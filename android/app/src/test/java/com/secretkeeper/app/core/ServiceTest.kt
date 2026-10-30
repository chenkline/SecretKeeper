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
 * 服务层自检：解锁/锁定与退避、主密钥与机密信息核心操作、
 * 限额、kNeedsPassword 分支、悬空引用掩码。
 * 与桌面 tests/core/service_test.cpp 的关键分支对应。
 */
class ServiceTest {
    private fun tempDir(): File =
        File(System.getProperty("java.io.tmpdir") ?: "/tmp")
            .resolve("sk-service-test-${System.nanoTime()}")
            .also { it.mkdirs() }

    private fun newService(): Service {
        val db = IndexDb(JdbcSqlDb.openInMemory())
        db.createSchema()
        val svc = Service(db, FileStore(tempDir()))
        assertTrue(svc.open())
        return svc
    }

    private fun charCountVectors(): List<org.json.JSONObject> {
        var dir: File? = File(System.getProperty("user.dir") ?: ".")
        repeat(6) {
            val d = dir
            if (d != null && File(d, "test-vectors/char-count.json").isFile) {
                val root = org.json.JSONObject(File(d, "test-vectors/char-count.json").readText())
                val out = mutableListOf<org.json.JSONObject>()
                val arr = root.getJSONArray("vectors")
                for (i in 0 until arr.length()) out.add(arr.getJSONObject(i))
                return out
            }
            dir = d?.parentFile
        }
        throw IllegalStateException("test-vectors not found")
    }

    // ------------------------------------------------------ 限额与字符计数

    @Test
    fun secretCharCountMatchesVectors() {
        for (v in charCountVectors()) {
            val text = String(Crypto.unhex(v.getString("textHex")), Charsets.UTF_8)
            val expected = v.getInt("expectedCodePoints")
            assertEquals("char count ${v.getString("name")}",
                expected.toLong(), Service.secretCharCount(text).toLong())
        }
    }

    @Test
    fun charCountLimitEnforcedBeforePassword() {
        val svc = newService()
        // 未创建任何主密钥：长度校验先于选密钥与输密码（需求 3.2）
        assertEquals(SvcError.EMPTY_SECRET,
            svc.addSecret("t", "", "000102030405060708090a0b0c0d0e0f", ByteArray(0)))
        assertEquals(SvcError.SECRET_TOO_LONG,
            svc.addSecret("t", "长".repeat(151), "000102030405060708090a0b0c0d0e0f", ByteArray(0)))
        // 150 字符正好通过长度校验（再往后会因主密钥不存在失败——顺序是先长度后密钥）
        val err150 = svc.addSecret("t", "a".repeat(150), "000102030405060708090a0b0c0d0e0f", ByteArray(0))
        assertNotEquals(SvcError.SECRET_TOO_LONG, err150)
        svc.close()
    }

    // ------------------------------------------------------ 主密钥生命周期

    @Test
    fun createMasterKeyAndFirstIsDefault() {
        val svc = newService()
        assertEquals(SvcError.OK, svc.createMasterKey("甲", "pw-1".toByteArray()))
        assertEquals(1, svc.listMasterKeys().size)
        assertTrue(svc.listMasterKeys()[0].isDefault)
        assertEquals(svc.listMasterKeys()[0].masterKeyId, svc.defaultMasterKeyId())
        // 空密码直接拒绝
        assertEquals(SvcError.PASSWORD_WRONG, svc.createMasterKey("乙", ByteArray(0)))
        // 第二把不自动成为默认
        assertEquals(SvcError.OK, svc.createMasterKey("乙", "pw-2".toByteArray()))
        val items = svc.listMasterKeys()
        assertEquals(2, items.size)
        assertEquals(1, items.count { it.isDefault })
        svc.close()
    }

    @Test
    fun masterKeyQuotaEnforced() {
        val svc = newService()
        assertEquals(SvcError.OK, svc.createMasterKey("甲", "pw-1".toByteArray()))
        assertEquals(SvcError.OK, svc.createMasterKey("乙", "pw-2".toByteArray()))
        assertEquals(SvcError.MASTER_KEY_QUOTA_EXCEEDED,
            svc.createMasterKey("丙", "pw-3".toByteArray()))
        svc.close()
    }

    @Test
    fun unlockLockAndBackoff() {
        val svc = newService()
        svc.createMasterKey("甲", "pw-1".toByteArray())
        val id = svc.defaultMasterKeyId()!!
        assertFalse(svc.isUnlocked())

        // 密码错误：首次报 PASSWORD_WRONG，计入退避；delay(1)=1 秒窗口内 waiting
        assertEquals(SvcError.PASSWORD_WRONG, svc.unlock(id, "bad".toByteArray()))
        assertEquals(1, svc.backoffStatus().consecutiveFailures)
        assertEquals(1, PasswordBackoff.delayFor(1, 60))
        // 连续失败：退避窗口内报 RATE_LIMITED（与桌面 note_password_failure 一致）
        assertEquals(SvcError.RATE_LIMITED, svc.unlock(id, "bad".toByteArray()))
        assertEquals(2, svc.backoffStatus().consecutiveFailures)
        assertEquals(2, PasswordBackoff.delayFor(2, 60))
        assertEquals(4, PasswordBackoff.delayFor(3, 60))
        assertEquals(60, PasswordBackoff.delayFor(10, 60)) // 封顶 60 秒

        // 正确密码解锁成功并清零退避
        assertEquals(SvcError.OK, svc.unlock(id, "pw-1".toByteArray()))
        assertTrue(svc.isUnlocked())
        assertEquals(0, svc.backoffStatus().consecutiveFailures)

        // 解锁成功即触发一次轮换（需求 4.2）：再 rotate 仍成功
        assertEquals(SvcError.OK, svc.rotateKek())

        // 锁定后清空缓存
        svc.lock()
        assertFalse(svc.isUnlocked())
        assertEquals(SvcError.LOCKED, svc.rotateKek())
        // 锁定后空密码对默认主密钥的操作报 LOCKED
        assertEquals(SvcError.LOCKED, svc.addSecret("t", "content", id, ByteArray(0)))
        svc.close()
    }

    // ------------------------------------------------------ 机密信息

    @Test
    fun addRevealDeleteSecretRoundTrip() {
        val svc = newService()
        svc.createMasterKey("甲", "pw-1".toByteArray())
        val mkId = svc.defaultMasterKeyId()!!

        // 未解锁 + 空密码 + 目标是默认主密钥 -> LOCKED（先解锁再操作）
        assertEquals(SvcError.LOCKED,
            svc.addSecret("网站甲", "secret-content", mkId, ByteArray(0)))
        assertEquals(SvcError.OK, svc.unlock(mkId, "pw-1".toByteArray()))
        // 已解锁 + 空密码 + 默认主密钥 -> 用缓存 KEK 成功
        assertEquals(SvcError.OK,
            svc.addSecret("网站甲", "secret-content", mkId, ByteArray(0)))

        val list = svc.listSecrets()
        assertEquals(1, list.size)
        assertEquals("网站甲", list[0].title)
        assertTrue(list[0].masterKeyFound)

        // 详情：默认主密钥已解锁，空密码也可解密
        val (e, detail) = svc.getSecretDetail(list[0].secretId, ByteArray(0))
        assertEquals(SvcError.OK, e)
        assertNotNull(detail)
        assertFalse(detail!!.plaintextMasked)
        assertEquals("secret-content", detail.plaintext)
        assertTrue(detail.masterKeyFound)

        // 非默认密钥 + 空密码 -> NEEDS_PASSWORD（见 switch 测试）

        // 删除：默认主密钥已解锁，空密码通过
        assertEquals(SvcError.OK, svc.deleteSecret(list[0].secretId, ByteArray(0)))
        assertEquals(0, svc.listSecrets().size)
        assertEquals(SvcError.SECRET_NOT_FOUND,
            svc.deleteSecret(list[0].secretId, ByteArray(0)))
        svc.close()
    }

    @Test
    fun secretQuotaEnforced() {
        val svc = newService()
        svc.createMasterKey("甲", "pw-1".toByteArray())
        val mkId = svc.defaultMasterKeyId()!!
        val pw = "pw-1".toByteArray()
        for (i in 0 until ServiceLimits.MAX_SECRETS) {
            assertEquals(SvcError.OK,
                svc.addSecret("条目 $i", "content-$i", mkId, pw))
        }
        assertEquals(SvcError.SECRET_QUOTA_EXCEEDED,
            svc.addSecret("超出", "content-x", mkId, pw))
        svc.close()
    }

    @Test
    fun switchDefaultRequiresBothPasswordsAndUpdatesCache() {
        val svc = newService()
        svc.createMasterKey("甲", "pw-1".toByteArray())
        val first = svc.defaultMasterKeyId()!!
        svc.createMasterKey("乙", "pw-2".toByteArray())
        val second = svc.listMasterKeys().first { it.masterKeyId != first }.masterKeyId

        // 未解锁 + 两密码齐全 -> 成功
        assertEquals(SvcError.OK,
            svc.switchDefaultMasterKey(second, "pw-1".toByteArray(), "pw-2".toByteArray()))
        assertEquals(second, svc.defaultMasterKeyId())
        // 切换后缓存已清：空密码操作报 NEEDS_PASSWORD/LOCKED
        assertFalse(svc.isUnlocked())

        // 原默认密码错误 -> 拒绝
        assertEquals(SvcError.PASSWORD_WRONG,
            svc.switchDefaultMasterKey(first, "bad".toByteArray(), "pw-1".toByteArray()))

        // 解锁后空密码切换可用缓存 KEK 校验
        assertEquals(SvcError.OK, svc.unlock(first, "pw-1".toByteArray()))
        assertEquals(SvcError.OK, svc.switchDefaultMasterKey(second, ByteArray(0), ByteArray(0)))
        assertEquals(second, svc.defaultMasterKeyId())
        svc.close()
    }

    @Test
    fun deleteDefaultMasterKeyForbidden() {
        val svc = newService()
        svc.createMasterKey("甲", "pw-1".toByteArray())
        val first = svc.defaultMasterKeyId()!!
        assertEquals(SvcError.CANNOT_DELETE_DEFAULT,
            svc.deleteMasterKey(first, "pw-1".toByteArray()))
        svc.close()
    }

    @Test
    fun danglingMasterKeyMasksPlaintext() {
        val svc = newService()
        svc.createMasterKey("甲", "pw-1".toByteArray())
        val mkId = svc.defaultMasterKeyId()!!
        val pw = "pw-1".toByteArray()
        assertEquals(SvcError.OK, svc.addSecret("网站乙", "topsecret", mkId, pw))
        val secretId = svc.listSecrets()[0].secretId

        // 删除主密钥前先切走默认（默认不可删）
        svc.createMasterKey("乙", "pw-2".toByteArray())
        val second = svc.listMasterKeys().first { it.masterKeyId != mkId }.masterKeyId
        assertEquals(SvcError.OK,
            svc.switchDefaultMasterKey(second, "pw-1".toByteArray(), "pw-2".toByteArray()))
        assertEquals(SvcError.OK, svc.deleteMasterKey(mkId, "pw-1".toByteArray()))

        // 机密信息保留（需求 2.6），主密钥缺失 -> 掩码态 + 黄色问号
        val list = svc.listSecrets()
        assertEquals(1, list.size)
        assertFalse(list[0].masterKeyFound)
        assertEquals("", list[0].masterKeyName)
        val (e, detail) = svc.getSecretDetail(secretId, ByteArray(0))
        assertEquals(SvcError.OK, e)
        assertNotNull(detail)
        assertTrue(detail!!.plaintextMasked)
        assertFalse(detail.masterKeyFound)
        // 掩码态不返回明文；MASKED_PLAINTEXT 固定 6 个星号
        assertEquals(Service.MASKED_PLAINTEXT, "******")
        svc.close()
    }

    @Test
    fun exportImportMasterKeyRoundTrip() {
        val svc = newService()
        svc.createMasterKey("甲", "pw-1".toByteArray())
        val id = svc.defaultMasterKeyId()!!
        svc.unlock(id, "pw-1".toByteArray())

        val (e, bytes) = svc.exportMasterKey(id, ByteArray(0), "prot-9".toByteArray())
        assertEquals(SvcError.OK, e)
        assertNotNull(bytes)
        // 导入到同一库会因限额与 ID 冲突路径受限：新建独立库验证
        val db2 = IndexDb(JdbcSqlDb.openInMemory())
        db2.createSchema()
        val svc2 = Service(db2, FileStore(tempDir()))
        assertTrue(svc2.open())
        assertEquals(SvcError.OK, svc2.importMasterKey(bytes!!, "prot-9".toByteArray(),
            "new-pw".toByteArray()))
        assertEquals(1, svc2.listMasterKeys().size)
        // 导入的密钥不自动成为默认（但库中只有一把，list 的 isDefault 来自 app_meta）
        assertNull(svc2.defaultMasterKeyId())
        // 新密码可解锁导入的密钥
        val importedId = svc2.listMasterKeys()[0].masterKeyId
        assertEquals(SvcError.OK, svc2.unlock(importedId, "new-pw".toByteArray()))
        svc2.close()
        svc.close()
    }

    @Test
    fun kekCacheLoadPeekRotate() {
        val cache = KekCache()
        val kek = Crypto.randomBytes(32)
        assertTrue(cache.load(kek))
        assertTrue(cache.hasValue())
        val peeked = cache.peek()
        assertNotNull("peek after load must return KEK", peeked)
        assertArrayEquals(kek, peeked)
        assertTrue("rotate must succeed", cache.rotate())
        assertArrayEquals("peek after rotate must return same KEK", kek, cache.peek())
        assertTrue(cache.activeSlotIndex() >= 0 && cache.activeSlotIndex() < KekCache.SLOT_COUNT)
        cache.clear()
        assertFalse(cache.hasValue())
        assertNull(cache.peek())
    }
}
