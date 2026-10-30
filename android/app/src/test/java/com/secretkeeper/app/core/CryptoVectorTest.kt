package com.secretkeeper.app.core

import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Android 端密码学原语黄金向量测试：读取仓库共享的 test-vectors/，
 * 与桌面三端（mbedTLS + vendor/argon2）跑同一份数据。
 *
 * OAEP 只测解密方向与尺寸上限（加密带随机种子不可跨端比对，
 * 见 AGENTS.md 9.8 的一致性探针规则）。
 */
class CryptoVectorTest {
    private fun repoRoot(): File {
        var dir: File? = File(System.getProperty("user.dir") ?: ".")
        repeat(6) {
            val d = dir ?: return@repeat
            val probe = File(d, "test-vectors/kdf-argon2id.json")
            if (probe.isFile) return d
            dir = d.parentFile
        }
        throw IllegalStateException("test-vectors not found from ${System.getProperty("user.dir")}")
    }

    private fun vectorRoot(): File = repoRoot().resolve("test-vectors")

    private fun readVector(file: String): JSONObject =
        JSONObject(File(vectorRoot(), file).readText()).getJSONArray("vectors").getJSONObject(0)

    private fun vectors(file: String): List<JSONObject> =
        JSONObject(File(vectorRoot(), file).readText()).getJSONArray("vectors")
            .map { it as JSONObject }

    private fun JSONObject.str(key: String): String = getString(key)

    @Test
    fun argon2idVectorsMatchDesktop() {
        val list = vectors("kdf-argon2id.json")
        assertTrue("no argon2id vectors", list.isNotEmpty())
        for (v in list) {
            val kdf = v.getJSONObject("kdf")
            val derived = Crypto.deriveKek(
                password = Crypto.unhex(kdf.str("passwordHex")),
                salt = Crypto.unhex(kdf.str("saltHex")),
                memoryKiB = kdf.getInt("memoryKiB"),
                iterations = kdf.getInt("iterations"),
                parallelism = kdf.getInt("parallelism"),
                outputLen = kdf.getInt("outputLen"),
            )
            assertArrayEquals(
                "argon2id vector ${v.str("name")} mismatch",
                Crypto.unhex(v.str("expectedHex").lowercase()),
                derived,
            )
        }
    }

    @Test
    fun aesGcmVectorsRoundTrip() {
        val list = vectors("aes-gcm.json")
        assertTrue("no aes-gcm vectors", list.isNotEmpty())
        for (v in list) {
            val key = Crypto.unhex(v.str("keyHex"))
            val nonce = Crypto.unhex(v.str("nonceHex"))
            val aad = Crypto.unhex(v.str("aadHex"))
            val plain = Crypto.unhex(v.str("plaintextHex"))
            val expectedCipher = Crypto.unhex(v.str("cipherHex"))
            val expectedTag = Crypto.unhex(v.str("tagHex"))

            val (cipher, tag) = Crypto.aesGcmEncrypt(key, nonce, aad, plain)
            assertArrayEquals("cipher ${v.str("name")}", expectedCipher, cipher)
            assertArrayEquals("tag ${v.str("name")}", expectedTag, tag)

            val decrypted = Crypto.aesGcmDecrypt(key, nonce, aad, cipher, tag)
            assertTrue("decrypt ${v.str("name")}", plain.contentEquals(decrypted))
        }
    }

    @Test
    fun aesGcmRejectsTamperedTag() {
        val v = vectors("aes-gcm.json")[0]
        val key = Crypto.unhex(v.str("keyHex"))
        val nonce = Crypto.unhex(v.str("nonceHex"))
        val aad = Crypto.unhex(v.str("aadHex"))
        val tag = Crypto.unhex(v.str("tagHex")).also { it[0] = (it[0].toInt() xor 1).toByte() }
        var threw = false
        try {
            Crypto.aesGcmDecrypt(key, nonce, aad, Crypto.unhex(v.str("cipherHex")), tag)
        } catch (expected: Exception) {
            threw = true
        }
        assertTrue("tampered tag must fail authentication", threw)
    }

    @Test
    fun rsaOaepDecryptsWrappedDataKey() {
        val v = readVector("rsa-oaep.json")
        assertEquals("sha256", v.str("oaepHash"))
        assertEquals("sha256", v.str("mgfHash"))

        val pem = v.str("keyPemPrivate")
        val base64 = pem.lines()
            .filter { !it.startsWith("-----") }
            .joinToString("")
        val privateDer = java.util.Base64.getDecoder().decode(base64)
        val dataKey = Crypto.rsaOaepDecrypt(privateDer, Crypto.unhex(v.str("wrappedHex")))
        assertEquals("wrappedLen", v.getInt("wrappedLen"), Crypto.unhex(v.str("wrappedHex")).size)
        assertTrue(
            "decrypted data key mismatch",
            Crypto.unhex(v.str("dataKeyHex")).contentEquals(dataKey),
        )
    }

    @Test
    fun rsaOaepEncryptDecryptRoundTripAndBounds() {
        val v = readVector("rsa-oaep.json")
        val pem = v.str("keyPemPublic")
        val base64 = pem.lines()
            .filter { !it.startsWith("-----") }
            .joinToString("")
        val publicDer = java.util.Base64.getDecoder().decode(base64)
        val privatePem = v.str("keyPemPrivate")
        val privateDer = java.util.Base64.getDecoder()
            .decode(privatePem.lines().filter { !it.startsWith("-----") }.joinToString(""))

        // 封装方向不可跨端比对（随机种子），这里验证自洽往返与尺寸上限
        val plain = ByteArray(190) { (it and 0xFF).toByte() }
        val wrapped = Crypto.rsaOaepEncrypt(publicDer, plain)
        assertEquals("wrappedLen", 256, wrapped.size)
        assertTrue("roundtrip", plain.contentEquals(Crypto.rsaOaepDecrypt(privateDer, wrapped)))

        var threw = false
        try {
            Crypto.rsaOaepEncrypt(publicDer, ByteArray(191))
        } catch (expected: IllegalArgumentException) {
            threw = true
        }
        assertTrue("plaintext over 190 bytes must be rejected", threw)
    }
}
