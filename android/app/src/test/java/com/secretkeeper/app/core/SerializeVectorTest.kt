package com.secretkeeper.app.core

import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 序列化层黄金向量测试：读取仓库共享的 test-vectors/，
 * 与桌面端（tests/core/serialize_test.cpp）跑同一份数据。
 */
class SerializeVectorTest {
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

    private fun readText(file: String): String =
        File(repoRoot(), "test-vectors/$file").readText()

    private fun ByteArray.hex(): String = Crypto.hex(this)

    @Test
    fun containerRoundtripMatchesDesktop() {
        val root = JSONObject(readText("container-roundtrip.json"))

        // --- 主密钥文件 ---
        val mkRoot = root.getJSONObject("masterKeyFile")
        val mkBytes = Crypto.unhex(mkRoot.getString("fileHex"))
        val mk = Serialize.parseMasterKey(mkBytes)
        assertTrue("master key file parsed", mk is Serialize.Result.Ok)
        val ok = mk as Serialize.Result.Ok
        val f = ok.value
        assertEquals("mkAlg == 1 (RSA-2048)", 1L, f.mkAlg)
        assertEquals(
            "masterKeyId matches vector",
            mkRoot.getJSONObject("expected").getString("masterKeyIdHex"),
            f.masterKeyId.hex(),
        )
        assertEquals("name matches vector", mkRoot.getJSONObject("expected").getString("name"), f.name)
        assertEquals(
            "salt matches vector",
            mkRoot.getJSONObject("expected").getString("saltHex"),
            f.salt.hex(),
        )
        assertEquals("kdfMem", 10240L, f.kdfMem)
        assertEquals("kdfIter", 3L, f.kdfIter)
        assertEquals("kdfPar", 1L, f.kdfPar)
        assertEquals(
            "pubKeyNonce matches vector",
            mkRoot.getJSONObject("expected").getString("pubKeyNonceHex"),
            f.pubKeyNonce.hex(),
        )
        assertEquals(
            "privKeyNonce matches vector",
            mkRoot.getJSONObject("expected").getString("privKeyNonceHex"),
            f.privKeyNonce.hex(),
        )
        assertFalse("pub/priv nonces differ", f.pubKeyNonce.contentEquals(f.privKeyNonce))
        val againMk = Serialize.serializeMasterKey(f)
        assertTrue("master key roundtrip is byte-identical", againMk.contentEquals(mkBytes))

        // --- 机密信息文件 ---
        val sfRoot = root.getJSONObject("secretFile")
        val sfBytes = Crypto.unhex(sfRoot.getString("fileHex"))
        val sf = Serialize.parseSecret(sfBytes)
        assertTrue("secret file parsed", sf is Serialize.Result.Ok)
        val sOk = sf as Serialize.Result.Ok
        val s = sOk.value
        val sExp = sfRoot.getJSONObject("expected")
        assertEquals("secretId matches vector", sExp.getString("secretIdHex"), s.secretId.hex())
        assertEquals("masterKeyId matches vector", sExp.getString("masterKeyIdHex"), s.masterKeyId.hex())
        assertEquals("title matches vector", sExp.getString("title"), s.title)
        assertEquals("wrappedDk length == 256", 256, s.wrappedDk.size)
        assertEquals(
            "dataNonce matches vector",
            sExp.getString("dataNonceHex"),
            s.dataNonce.hex(),
        )
        val againSf = Serialize.serializeSecret(s)
        assertTrue("secret roundtrip is byte-identical", againSf.contentEquals(sfBytes))
    }

    @Test
    fun negativeCasesRejectedWithExpectedCategory() {
        val root = JSONObject(readText("negative-cases.json"))
        val vectors = root.getJSONArray("vectors")
        assertTrue("negative case count >= 10", vectors.length() >= 10)
        var checked = 0
        for (i in 0 until vectors.length()) {
            val v = vectors.getJSONObject(i)
            val name = v.getString("name")
            val expect = v.getString("expectedError")
            if (v.has("keyHex")) {
                // GCM 认证类向量：密文/口令错 -> 认证失败，容器解析不适用
                val key = Crypto.unhex(v.getString("keyHex"))
                val nonce = Crypto.unhex(v.getString("nonceHex"))
                val aad = Crypto.unhex(v.getString("aadHex"))
                val cipher = Crypto.unhex(v.getString("cipherHex"))
                val tag = Crypto.unhex(v.getString("tagHex"))
                var threw = false
                try {
                    Crypto.aesGcmDecrypt(key, nonce, aad, cipher, tag)
                } catch (expected: Exception) {
                    threw = true
                }
                assertTrue("$name: GCM must reject", threw)
                checked++
                continue
            }
            val bytes = Crypto.unhex(v.getString("inputHex"))
            val asMaster = Serialize.parseMasterKey(bytes)
            val asSecret = Serialize.parseSecret(bytes)
            // 只要有一条路径报出期望的错误类别即算通过（与桌面测试一致）
            val e1 = (asMaster as? Serialize.Result.Err)?.error
            val e2 = (asSecret as? Serialize.Result.Err)?.error
            assertTrue(
                "$name: must be rejected by at least one parser",
                asMaster is Serialize.Result.Err || asSecret is Serialize.Result.Err,
            )
            val m1 = e1?.name?.lowercase()?.let { categoryMatches(it, expect) } == true
            val m2 = e2?.name?.lowercase()?.let { categoryMatches(it, expect) } == true
            assertTrue(
                "$name: error category should be $expect (got $e1 / $e2)",
                m1 || m2,
            )
            checked++
        }
        assertTrue("at least 11 cases executed", checked >= 11)
    }

    /** 枚举名 snake_case 化后与向量期望对齐。 */
    private fun categoryMatches(enumName: String, expect: String): Boolean =
        enumName.lowercase() == expect.lowercase()

    @Test
    fun parsedObjectsRoundTripIndependently() {
        // 额外自检：从往返向量取对象，改动 name 后重序列化，再解析回读
        val root = JSONObject(readText("container-roundtrip.json"))
        val mkBytes = Crypto.unhex(root.getJSONObject("masterKeyFile").getString("fileHex"))
        val f = (Serialize.parseMasterKey(mkBytes) as Serialize.Result.Ok).value
        val renamed = f.copy(name = "renamed-key")
        val bytes2 = Serialize.serializeMasterKey(renamed)
        val back = Serialize.parseMasterKey(bytes2) as Serialize.Result.Ok
        assertEquals("renamed-key", back.value.name)
        assertNotEquals(mkBytes, bytes2)
        assertEquals(f.masterKeyId.hex(), back.value.masterKeyId.hex())
    }
}
