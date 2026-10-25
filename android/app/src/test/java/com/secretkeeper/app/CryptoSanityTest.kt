package com.secretkeeper.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CryptoSanityTest {
    @Test
    fun `random bytes are the requested length`() {
        val bytes = ByteArray(16)
        java.security.SecureRandom().nextBytes(bytes)
        assertEquals(16, bytes.size)
    }

    @Test
    fun `android supports aes gcm`() {
        val generator = javax.crypto.KeyGenerator.getInstance("AES")
        generator.init(256)
        val cipher = javax.crypto.Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(javax.crypto.Cipher.ENCRYPT_MODE, generator.generateKey())
        val output = cipher.doFinal("probe".toByteArray())
        assertTrue(output.size > "probe".toByteArray().size)
    }
}
