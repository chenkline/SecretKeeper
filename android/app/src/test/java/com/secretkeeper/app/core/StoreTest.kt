package com.secretkeeper.app.core

import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 存储层自检：索引库 CRUD + upsert 保留 created_at + 悬空引用 +
 * 文件原子写 + 对象级 save/load 往返。SQL 走 xerial sqlite-jdbc 真 SQLite。
 */
class StoreTest {
    private fun tempDir(): File =
        File(System.getProperty("java.io.tmpdir") ?: "/tmp")
            .resolve("sk-store-test-${System.nanoTime()}")
            .also { it.mkdirs() }

    private fun openDb(): IndexDb = IndexDb(JdbcSqlDb.openInMemory()).also { it.createSchema() }

    private fun sampleMasterKey(idHex: String): Serialize.MasterKeyFile {
        val mkBytes = Crypto.unhex(
            File(repoRoot(), "test-vectors/container-roundtrip.json").readText()
                .let { org.json.JSONObject(it).getJSONObject("masterKeyFile").getString("fileHex") },
        )
        val f = (Serialize.parseMasterKey(mkBytes) as Serialize.Result.Ok).value
        return if (idHex == Ids.toHex(f.masterKeyId)) {
            f
        } else {
            // 换 ID 构造第二条记录（其余字段复用向量值，ID 用固定字节填充）
            f.copy(masterKeyId = Ids.fromHex(idHex)!!)
        }
    }

    private fun repoRoot(): File {
        var dir: File? = File(System.getProperty("user.dir") ?: ".")
        repeat(6) {
            val d = dir ?: return@repeat
            val probe = File(d, "test-vectors/kdf-argon2id.json")
            if (probe.isFile) return d
            dir = d.parentFile
        }
        throw IllegalStateException("test-vectors not found")
    }

    private fun sampleSecret(idHex: String, mkIdHex: String): Serialize.SecretFile {
        val sfBytes = Crypto.unhex(
            File(repoRoot(), "test-vectors/container-roundtrip.json").readText()
                .let { org.json.JSONObject(it).getJSONObject("secretFile").getString("fileHex") },
        )
        val s = (Serialize.parseSecret(sfBytes) as Serialize.Result.Ok).value
        return s.copy(secretId = Ids.fromHex(idHex)!!, masterKeyId = Ids.fromHex(mkIdHex)!!)
    }

    @Test
    fun idHexRoundTripStrict() {
        val id = Ids.fromHex("000102030405060708090a0b0c0d0e0f")
        assertNotNull(id)
        assertEquals("000102030405060708090a0b0c0d0e0f", Ids.toHex(id!!))
        // 奇数长度 / 非法字符 / 长度不符一律拒绝
        assertNull(Ids.fromHex("0011"))
        assertNull(Ids.fromHex("zz110102030405060708090a0b0c0d0e0f"))
        assertNull(Ids.fromHex("000102030405060708090a0b0c0d0e0f00"))
        assertNull(Ids.fromHex(""))
    }

    @Test
    fun indexUpsertKeepsCreatedAt() {
        val db = openDb()
        val mk = sampleMasterKey("110102030405060708090a0b0c0d0e0f")
        val idHex = Ids.toHex(mk.masterKeyId)
        val store = MasterKeyStore(db, FileStore(tempDir()))
        assertEquals(SaveError.OK, store.save(mk, "first", makeDefault = true))
        val row1 = db.findMasterKey(idHex)!!
        val created1 = row1.createdAt
        assertTrue(created1 > 0)
        Thread.sleep(5) // 保证 updated_at 可分辨
        // upsert 更新名称但保留 created_at
        assertEquals(SaveError.OK, store.save(mk, "renamed", makeDefault = false))
        val row2 = db.findMasterKey(idHex)!!
        assertEquals("renamed", row2.name)
        assertEquals(created1, row2.createdAt)
        assertEquals(1, db.masterKeyCount())
        db.close()
    }

    @Test
    fun defaultMasterKeyPersistedInMeta() {
        val db = openDb()
        val files = FileStore(tempDir())
        val store = MasterKeyStore(db, files)
        val a = sampleMasterKey("110102030405060708090a0b0c0d0e0f")
        val b = sampleMasterKey("220102030405060708090a0b0c0d0e0f")
        assertEquals(SaveError.OK, store.save(a, "a", makeDefault = true))
        assertEquals(SaveError.OK, store.save(b, "b", makeDefault = false))
        assertEquals(Ids.toHex(a.masterKeyId), db.defaultMasterKeyId())
        // 切换默认
        assertEquals(SaveError.OK, store.save(b, "b", makeDefault = true))
        assertEquals(Ids.toHex(b.masterKeyId), db.defaultMasterKeyId())
        // 默认唯一性
        val defaults = db.listMasterKeys().count { it.isDefault }
        assertEquals(1, defaults)
        db.close()
    }

    @Test
    fun masterKeyLoadRoundTripViaFileAndIndex() {
        val db = openDb()
        val dir = tempDir()
        assertTrue(dir.resolve("keys").let { it.isDirectory || it.mkdirs() })
        val store = MasterKeyStore(db, FileStore(dir))
        val mk = sampleMasterKey("110102030405060708090a0b0c0d0e0f")
        // 文件与索引同名（桌面上层保证二者一致，这里构造同样的一致性）
        assertEquals(SaveError.OK, store.save(mk.copy(name = "主密钥甲"), "主密钥甲", makeDefault = true))
        val (err, loaded) = store.load(Ids.toHex(mk.masterKeyId))
        assertEquals(SaveError.OK, err)
        assertNotNull(loaded)
        assertArrayEquals(mk.masterKeyId, loaded!!.masterKeyId)
        assertEquals("主密钥甲", loaded.name)
        assertArrayEquals(mk.salt, loaded.salt)
        assertArrayEquals(mk.privKeyCipher, loaded.privKeyCipher)
        // 不存在的 ID
        assertEquals(SaveError.NOT_FOUND, store.load("330102030405060708090a0b0c0d0e0f").first)
        // 非法 ID
        assertEquals(SaveError.INVALID_ARGUMENT, store.load("xyz").first)
        db.close()
    }

    @Test
    fun secretLoadRoundTripAndDanglingReference() {
        val db = openDb()
        val dir = tempDir()
        val store = SecretStore(db, FileStore(dir))
        val mkId = "110102030405060708090a0b0c0d0e0f"
        val secret = sampleSecret("440102030405060708090a0b0c0d0e0f", mkId)
        assertEquals(SaveError.OK, store.save(secret))
        val (err, loaded) = store.load(Ids.toHex(secret.secretId))
        assertEquals(SaveError.OK, err)
        assertNotNull(loaded)
        assertEquals(Ids.toHex(secret.secretId), Ids.toHex(loaded!!.secretId))
        assertEquals(mkId, loaded.masterKeyId.let { Ids.toHex(it) })
        assertEquals(secret.title, loaded.title)
        assertArrayEquals(secret.dataCipher, loaded.dataCipher)
        // 悬空引用：索引里有 secret 引用的 master_key 不存在于 master_keys
        assertEquals(1, db.listSecretsByMasterKey(mkId).size)
        assertNull(db.findMasterKey(mkId)) // 主密钥不在库中——悬空是合法状态
        db.close()
    }

    @Test
    fun deleteRemovesIndexAndCountFollows() {
        val db = openDb()
        val store = SecretStore(db, FileStore(tempDir()))
        val mkId = "110102030405060708090a0b0c0d0e0f"
        val s1 = sampleSecret("440102030405060708090a0b0c0d0e0f", mkId)
        val s2 = sampleSecret("550102030405060708090a0b0c0d0e0f", mkId)
        store.save(s1)
        store.save(s2)
        assertEquals(2, db.secretCount())
        assertEquals(2, db.countSecretsForMasterKey(mkId))
        assertEquals(StoreError.OK, db.deleteSecret(Ids.toHex(s1.secretId)))
        assertEquals(StoreError.NOT_FOUND, db.deleteSecret(Ids.toHex(s1.secretId)))
        assertEquals(1, db.secretCount())
        assertEquals(1, db.countSecretsForMasterKey(mkId))
        db.close()
    }

    @Test
    fun fileStoreAtomicWriteAndLayout() {
        val dir = tempDir()
        val files = FileStore(dir)
        assertTrue(files.ensureLayout())
        assertTrue(File(dir, "keys").isDirectory)
        assertTrue(File(dir, "secrets").isDirectory)
        // 路径推导由 ID 派生、纯 ASCII
        assertEquals("keys/110102030405060708090a0b0c0d0e0f.smk",
            files.masterKeyRelPath("110102030405060708090a0b0c0d0e0f"))
        assertEquals("secrets/440102030405060708090a0b0c0d0e0f.ssc",
            files.secretRelPath("440102030405060708090a0b0c0d0e0f"))
        // 原子写 + 读回 + 覆盖写 + 删除
        val bytes1 = byteArrayOf(1, 2, 3)
        assertTrue(files.writeAtomic("keys/test.smk", bytes1))
        assertArrayEquals(bytes1, files.read("keys/test.smk"))
        val bytes2 = byteArrayOf(9, 8, 7, 6)
        assertTrue(files.writeAtomic("keys/test.smk", bytes2))
        assertArrayEquals(bytes2, files.read("keys/test.smk"))
        // 临时文件不残留
        assertFalse(File(dir, "keys/test.smk.tmp").exists())
        assertTrue(files.remove("keys/test.smk"))
        assertNull(files.read("keys/test.smk"))
        // remove 不存在的文件也算成功（幂等）
        assertTrue(files.remove("keys/test.smk"))
    }

    @Test
    fun parseErrorPropagatesAsSaveError() {
        val db = openDb()
        val dir = tempDir()
        val files = FileStore(dir)
        files.ensureLayout()
        val store = MasterKeyStore(db, files)
        val idHex = "660102030405060708090a0b0c0d0e0f"
        // 先正常保存登记索引，再篡改文件字节——load 读到坏数据应报 PARSE_ERROR
        val mk = sampleMasterKey(idHex)
        assertEquals(SaveError.OK, store.save(mk, "x", makeDefault = false))
        File(dir, "keys/$idHex.smk").writeBytes(
            Crypto.unhex("53534331000100000000016e000000100f0f0f0f"),
        )
        val (err, _) = store.load(idHex)
        assertEquals(SaveError.PARSE_ERROR, err)
        // 文件被删但索引还在 → IO_ERROR（与桌面 store.cpp 语义一致）
        assertTrue(files.remove("keys/$idHex.smk"))
        assertEquals(SaveError.IO_ERROR, store.load(idHex).first)
        db.close()
    }

    @Test
    fun listOrderIsStableByCreatedAt() {
        val db = openDb()
        val store = SecretStore(db, FileStore(tempDir()))
        val mkId = "110102030405060708090a0b0c0d0e0f"
        val a = sampleSecret("440102030405060708090a0b0c0d0e0f", mkId)
        val b = sampleSecret("550102030405060708090a0b0c0d0e0f", mkId)
        store.save(a)
        store.save(b)
        val list = db.listSecrets()
        assertEquals(2, list.size)
        assertNotEquals(list[0].secretId, list[1].secretId)
        assertTrue(list[0].createdAt <= list[1].createdAt)
        db.close()
    }
}
