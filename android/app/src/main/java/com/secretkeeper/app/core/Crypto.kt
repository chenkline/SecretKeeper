package com.secretkeeper.app.core

import java.security.KeyFactory
import java.security.SecureRandom
import java.security.spec.MGF1ParameterSpec
import java.security.spec.PKCS8EncodedKeySpec
import javax.crypto.spec.PSource
import java.security.spec.X509EncodedKeySpec
import javax.crypto.Cipher
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.OAEPParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * Android 端密码学原语（移动端原生实现，铁律二）。
 *
 * 与桌面三端（mbedTLS + vendor/argon2）逐字节对齐的验证方式：
 * [com.secretkeeper.app.core.CryptoVectorTest] 读取仓库共享的
 * test-vectors/ 黄金向量并逐项断言。
 *
 * 约定（与 src/include/core/crypto.h 相同）：
 * - Argon2id：m=10240KiB、t=3、p=1，输出 32 字节 KEK，盐随记录存储。
 * - AES-256-GCM：nonce 12 字节、tag 16 字节、AAD 分别为主密钥 ID / 机密 ID。
 * - RSA-OAEP-SHA256：MGF1 也用 SHA-256，label 为空，封装 32 字节数据密钥。
 *
 * Argon2id 为纯 Kotlin 移植，移植基准是 vendor/argon2/src/ref.c 与
 * core.c（逐函数对照，不引入任何第三方依赖）；正确性由黄金向量兜底。
 */
object Crypto {
    const val ARGON2_VERSION = 0x13
    const val TAG_BITS = 128
    const val NONCE_LEN = 12
    const val TAG_LEN = 16
    const val WRAPPED_LEN = 256
    const val MAX_OAEP_PLAINTEXT = 190 // 256 - 2*32 - 2

    private val random = SecureRandom()

    fun randomBytes(n: Int): ByteArray = ByteArray(n).also { random.nextBytes(it) }

    fun hex(bytes: ByteArray): String = buildString(bytes.size * 2) {
        for (b in bytes) {
            append("0123456789abcdef"[(b.toInt() shr 4) and 0xF])
            append("0123456789abcdef"[b.toInt() and 0xF])
        }
    }

    fun unhex(s: String): ByteArray {
        val t = s.trim().lowercase()
        require(t.length % 2 == 0) { "odd hex length" }
        return ByteArray(t.length / 2) { i ->
            ((Character.digit(t[2 * i], 16) shl 4) or Character.digit(t[2 * i + 1], 16)).toByte()
        }
    }

    fun constantEquals(a: ByteArray, b: ByteArray): Boolean {
        if (a.size != b.size) return false
        var diff = 0
        for (i in a.indices) diff = diff or (a[i].toInt() xor b[i].toInt())
        return diff == 0
    }

    // ---- Argon2id（密码 → KEK）----

    /**
     * Argon2id 密钥派生。参数与桌面端一致：m=10240KiB、t=3、p=1、输出 32 字节。
     * 移植基准：vendor/argon2/src/ref.c（fill_segment / index_alpha /
     * fill_block / next_addresses）与 core.c（initial_hash / fill_first_blocks /
     * finalize）、blake2/blake2b.c（blake2b_long）。
     */
    fun deriveKek(
        password: ByteArray,
        salt: ByteArray,
        memoryKiB: Int = 10240,
        iterations: Int = 3,
        parallelism: Int = 1,
        outputLen: Int = 32,
    ): ByteArray = Argon2id.derive(
        password, salt, memoryKiB, iterations, parallelism, outputLen,
    )

    // ---- AES-256-GCM ----

    /** 返回 (密文, tag)；与桌面端容器布局一致，密文与 tag 分开保存。 */
    fun aesGcmEncrypt(
        key: ByteArray,
        nonce: ByteArray,
        aad: ByteArray,
        plaintext: ByteArray,
    ): Pair<ByteArray, ByteArray> {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(TAG_BITS, nonce))
        if (aad.isNotEmpty()) cipher.updateAAD(aad)
        val out = cipher.doFinal(plaintext) // Java 约定：密文 || tag
        val ct = out.copyOfRange(0, out.size - TAG_LEN)
        val tag = out.copyOfRange(out.size - TAG_LEN, out.size)
        return Pair(ct, tag)
    }

    /** 认证失败抛 AEADBadTagException；调用方按需求统一映射提示文案。 */
    fun aesGcmDecrypt(
        key: ByteArray,
        nonce: ByteArray,
        aad: ByteArray,
        ciphertext: ByteArray,
        tag: ByteArray,
    ): ByteArray {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(TAG_BITS, nonce))
        if (aad.isNotEmpty()) cipher.updateAAD(aad)
        return cipher.doFinal(ciphertext + tag)
    }

    // ---- RSA-2048 OAEP-SHA256（MGF1-SHA256）----

    /**
     * 生成主密钥密钥对。返回 (私钥 PKCS#8 DER, 公钥 SPKI DER)；
     * 序列化格式与桌面端一致（PKCS#8 / SPKI）。
     */
    fun rsaGenerateKeyPair(modulusBits: Int = 2048): Pair<ByteArray, ByteArray> {
        val generator = java.security.KeyPairGenerator.getInstance("RSA")
        generator.initialize(modulusBits)
        val pair = generator.generateKeyPair()
        return Pair(pair.private.encoded, pair.public.encoded)
    }

    /** 封装数据密钥：RSA-OAEP-SHA256，MGF1 同为 SHA-256，label 为空。 */
    fun rsaOaepEncrypt(publicSpki: ByteArray, plaintext: ByteArray): ByteArray {
        require(plaintext.size <= MAX_OAEP_PLAINTEXT) { "OAEP plaintext too long" }
        val public = KeyFactory.getInstance("RSA").generatePublic(X509EncodedKeySpec(publicSpki))
        val cipher = Cipher.getInstance("RSA/ECB/OAEPWithSHA-256AndMGF1Padding")
        // JVM 默认 MGF1 用 SHA-1，必须显式指定与桌面 mbedTLS 一致的 MGF1-SHA256
        cipher.init(
            Cipher.ENCRYPT_MODE, public,
            OAEPParameterSpec("SHA-256", "MGF1", MGF1ParameterSpec.SHA256, PSource.PSpecified.DEFAULT),
        )
        return cipher.doFinal(plaintext)
    }

    /** 解封装数据密钥。封装格式错误抛 BadPaddingException 等运行时异常。 */
    fun rsaOaepDecrypt(privatePkcs8: ByteArray, wrapped: ByteArray): ByteArray {
        val private = KeyFactory.getInstance("RSA").generatePrivate(PKCS8EncodedKeySpec(privatePkcs8))
        val cipher = Cipher.getInstance("RSA/ECB/OAEPWithSHA-256AndMGF1Padding")
        cipher.init(
            Cipher.DECRYPT_MODE, private,
            OAEPParameterSpec("SHA-256", "MGF1", MGF1ParameterSpec.SHA256, PSource.PSpecified.DEFAULT),
        )
        return cipher.doFinal(wrapped)
    }
}

/**
 * Argon2id 纯 Kotlin 移植（RFC 9106），函数与 vendor/argon2/src/ref.c、
 * core.c 一一对应，命名尽量保留 C 侧原名便于对照审阅。
 */
private object Argon2id {
    const val TYPE_ARGON2ID = 2
    const val SYNC_POINTS = 4
    const val ADDRESSES_IN_BLOCK = 128
    const val BLOCK_BYTES = 1024
    const val PREHASH_DIGEST_LEN = 64
    const val PREHASH_SEED_LEN = 72

    fun derive(
        password: ByteArray,
        salt: ByteArray,
        memoryKiB: Int,
        iterations: Int,
        lanes: Int,
        outputLen: Int,
    ): ByteArray {
        var memoryBlocks = memoryKiB
        if (memoryBlocks < 2 * SYNC_POINTS * lanes) {
            memoryBlocks = 2 * SYNC_POINTS * lanes
        }
        var segmentLength = memoryBlocks / (lanes * SYNC_POINTS)
        memoryBlocks = segmentLength * (lanes * SYNC_POINTS) // 保证各段等长
        val laneLength = segmentLength * SYNC_POINTS

        // initial_hash（core.c）：H0 = Blake2b-512(全部输入参数)
        val h0 = Blake2b(PREHASH_DIGEST_LEN).apply {
            updateLe32(lanes)
            updateLe32(outputLen)
            updateLe32(memoryKiB)
            updateLe32(iterations)
            updateLe32(Crypto.ARGON2_VERSION)
            updateLe32(TYPE_ARGON2ID)
            updateLe32(password.size)
            update(password)
            updateLe32(salt.size)
            update(salt)
            updateLe32(0) // secretlen（本工程不使用）
            updateLe32(0) // adlen
        }.final()

        // memory[lane][index] = 128 × u64
        val memory = Array(lanes) { Array(laneLength) { LongArray(128) } }

        // fill_first_blocks：B[l][0..1] = H'(H0 || LE32(j) || LE32(l))
        for (lane in 0 until lanes) {
            for (j in 0..1) {
                val seed = h0.copyOf(PREHASH_SEED_LEN)
                store32(seed, PREHASH_DIGEST_LEN, j)
                store32(seed, PREHASH_DIGEST_LEN + 4, lane)
                val blockBytes = blake2bLong(BLOCK_BYTES, seed)
                loadBlock(memory[lane][j], blockBytes)
            }
        }

        // fill_memory_blocks（串行，与 ref.c 单线程路径一致）
        for (pass in 0 until iterations) {
            for (slice in 0 until SYNC_POINTS) {
                for (lane in 0 until lanes) {
                    fillSegment(memory, lanes, iterations, pass, slice, lane,
                        segmentLength, laneLength, memoryBlocks)
                }
            }
        }

        // finalize：各 lane 末块 XOR 后 blake2bLong(outLen, ...)
        val blockhash = memory[0][laneLength - 1].copyOf()
        for (lane in 1 until lanes) {
            val last = memory[lane][laneLength - 1]
            for (i in 0 until 128) blockhash[i] = blockhash[i] xor last[i]
        }
        val blockBytes = ByteArray(BLOCK_BYTES)
        storeBlock(blockBytes, blockhash)
        return blake2bLong(outputLen, blockBytes)
    }

    private fun fillSegment(
        memory: Array<Array<LongArray>>,
        lanes: Int,
        passes: Int,
        pass: Int,
        slice: Int,
        lane: Int,
        segmentLength: Int,
        laneLength: Int,
        memoryBlocks: Int,
    ) {
        // ref.c：Argon2id 只在第一遍的前两个分片用数据独立寻址
        val dataIndependentAddressing = pass == 0 && slice < SYNC_POINTS / 2
        val address = LongArray(128)
        val input = LongArray(128)
        val zero = LongArray(128)
        if (dataIndependentAddressing) {
            input[0] = pass.toLong()
            input[1] = lane.toLong()
            input[2] = slice.toLong()
            input[3] = memoryBlocks.toLong()
            input[4] = passes.toLong()
            input[5] = TYPE_ARGON2ID.toLong()
        }

        var startingIndex = 0
        if (pass == 0 && slice == 0) {
            startingIndex = 2 // 前两块已由 H' 生成
            if (dataIndependentAddressing) nextAddresses(address, input, zero)
        }

        var currOffset = lane * laneLength + slice * segmentLength + startingIndex
        var prevOffset = if (currOffset % laneLength == 0) {
            currOffset + laneLength - 1
        } else {
            currOffset - 1
        }

        var i = startingIndex
        while (i < segmentLength) {
            if (currOffset % laneLength == 1) prevOffset = currOffset - 1

            val pseudoRand: Long
            if (dataIndependentAddressing) {
                if (i % ADDRESSES_IN_BLOCK == 0) nextAddresses(address, input, zero)
                pseudoRand = address[i % ADDRESSES_IN_BLOCK]
            } else {
                pseudoRand = blockAt(memory, laneLength, prevOffset)[0]
            }

            var refLane = ((pseudoRand ushr 32) % lanes.toLong()).toInt()
            if (pass == 0 && slice == 0) refLane = lane // 第一遍首分片不能跨 lane

            val refIndex = indexAlpha(
                pass, slice, i, laneLength, segmentLength,
                pseudoRand and 0xFFFFFFFFL, refLane == lane, lanes,
            )

            val refBlock = blockAt(memory, laneLength, refLane * laneLength + refIndex)
            val currBlock = blockAt(memory, laneLength, currOffset)
            fillBlock(blockAt(memory, laneLength, prevOffset), refBlock, currBlock,
                withXor = pass != 0)
            ++i
            ++currOffset
            ++prevOffset
        }
    }

    // core.c index_alpha：伪随机值平方映射到引用区
    private fun indexAlpha(
        pass: Int,
        slice: Int,
        index: Int,
        laneLength: Int,
        segmentLength: Int,
        pseudoRand: Long, // 已 & 0xFFFFFFFF
        sameLane: Boolean,
        lanes: Int,
    ): Int {
        val referenceAreaSize: Int = if (pass == 0) {
            if (slice == 0) {
                index - 1
            } else if (sameLane) {
                slice * segmentLength + index - 1
            } else {
                slice * segmentLength + (if (index == 0) -1 else 0)
            }
        } else if (sameLane) {
            laneLength - segmentLength + index - 1
        } else {
            laneLength - segmentLength + (if (index == 0) -1 else 0)
        }

        var relative = pseudoRand
        relative = (relative * relative) ushr 32
        val mapped = ((referenceAreaSize.toLong() and 0xFFFFFFFFL) * (relative and 0xFFFFFFFFL)) ushr 32
        val relativePosition = referenceAreaSize - 1 - mapped.toInt()

        val startPosition = if (pass != 0) {
            if (slice == SYNC_POINTS - 1) 0 else (slice + 1) * segmentLength
        } else {
            0
        }
        return (startPosition + relativePosition) % laneLength
    }

    // C 侧 memory 是扁平块数组，flat 偏移 = lane*laneLength + index；
    // Kotlin 侧用 [lane][index] 二维数组存储，此处负责换算。
    private fun blockAt(
        memory: Array<Array<LongArray>>,
        laneLength: Int,
        flat: Int,
    ): LongArray = memory[flat / laneLength][flat % laneLength]

    // ref.c next_addresses：G(zero, input) → G(zero, address) 两步推进地址块
    private fun nextAddresses(address: LongArray, input: LongArray, zero: LongArray) {
        input[6]++
        fillBlock(zero, input, address, withXor = false)
        fillBlock(zero, address, address, withXor = false)
    }

    // ref.c fill_block：R = ref XOR prev；列轮 + 行轮 blamka；next = tmp XOR R
    private fun fillBlock(prev: LongArray, ref: LongArray, next: LongArray, withXor: Boolean) {
        val blockR = LongArray(128)
        val blockTmp = LongArray(128)
        for (i in 0 until 128) blockR[i] = ref[i] xor prev[i]
        for (i in 0 until 128) blockTmp[i] = blockR[i]
        if (withXor) {
            for (i in 0 until 128) blockTmp[i] = blockTmp[i] xor next[i]
        }
        for (i in 0 until 8) roundColumn(blockR, 16 * i)
        for (i in 0 until 8) roundRow(blockR, i)
        for (i in 0 until 128) next[i] = blockTmp[i] xor blockR[i]
    }

    // blamka-round-ref.h：fBlaMka + G + BLAKE2_ROUND_NOMSG
    private fun fBlaMka(x: Long, y: Long): Long {
        val m = 0xFFFFFFFFL
        val xy = (x and m) * (y and m) // 与 C 的无符号乘法同样按 2^64 截断
        return x + y + 2 * xy
    }

    private fun gRound(v: LongArray, a: Int, b: Int, c: Int, d: Int) {
        v[a] = fBlaMka(v[a], v[b])
        v[d] = (v[d] xor v[a]).rotateRight(32)
        v[c] = fBlaMka(v[c], v[d])
        v[b] = (v[b] xor v[c]).rotateRight(24)
        v[a] = fBlaMka(v[a], v[b])
        v[d] = (v[d] xor v[a]).rotateRight(16)
        v[c] = fBlaMka(v[c], v[d])
        v[b] = (v[b] xor v[c]).rotateRight(63)
    }

    private fun roundColumn(v: LongArray, o: Int) {
        gRound(v, o, o + 4, o + 8, o + 12)
        gRound(v, o + 1, o + 5, o + 9, o + 13)
        gRound(v, o + 2, o + 6, o + 10, o + 14)
        gRound(v, o + 3, o + 7, o + 11, o + 15)
        gRound(v, o, o + 5, o + 10, o + 15)
        gRound(v, o + 1, o + 6, o + 11, o + 12)
        gRound(v, o + 2, o + 7, o + 8, o + 13)
        gRound(v, o + 3, o + 4, o + 9, o + 14)
    }

    private fun roundRow(v: LongArray, o: Int) {
        // BLAKE2_ROUND_NOMSG 的 G 序列作用在行排列 rowperm = [x, x+1, x+16,
        // x+17, x+32, x+33, ..., x+112, x+113]（x = 2*o）上：
        // G(rowperm[k], rowperm[k+4], rowperm[k+8], rowperm[k+12])。
        // 关键映射：G6 = rowperm[1,6,11,12] = (x+1, x+48, x+81, x+96)；
        //          G8 = rowperm[3,4,9,14]  = (x+17, x+32, x+65, x+112)。
        // G6 的第三参与 G8 的第三参极易写反（65/81），须以预处理展开为准。
        val x = 2 * o
        gRound(v, x, x + 32, x + 64, x + 96)
        gRound(v, x + 1, x + 33, x + 65, x + 97)
        gRound(v, x + 16, x + 48, x + 80, x + 112)
        gRound(v, x + 17, x + 49, x + 81, x + 113)
        gRound(v, x, x + 33, x + 80, x + 113)
        gRound(v, x + 1, x + 48, x + 81, x + 96)
        gRound(v, x + 16, x + 49, x + 64, x + 97)
        gRound(v, x + 17, x + 32, x + 65, x + 112)
    }

    private fun store32(dst: ByteArray, off: Int, value: Int) {
        dst[off] = (value and 0xFF).toByte()
        dst[off + 1] = ((value shr 8) and 0xFF).toByte()
        dst[off + 2] = ((value shr 16) and 0xFF).toByte()
        dst[off + 3] = ((value shr 24) and 0xFF).toByte()
    }

    private fun loadBlock(block: LongArray, bytes: ByteArray) {
        for (i in 0 until 128) {
            var v = 0L
            for (j in 7 downTo 0) v = (v shl 8) or (bytes[8 * i + j].toLong() and 0xFF)
            block[i] = v
        }
    }

    private fun storeBlock(bytes: ByteArray, block: LongArray) {
        for (i in 0 until 128) {
            var v = block[i]
            for (j in 0 until 8) {
                bytes[8 * i + j] = (v and 0xFF).toByte()
                v = v ushr 8
            }
        }
    }
}

/**
 * Blake2b（RFC 7693），流式接口，摘要长度 1..64。
 * IV / SIGMA / G 轮函数与 vendor/argon2/src/blake2/blake2b.c 一致。
 */
private class Blake2b(private val outlen: Int) {
    private val h = LongArray(8)
    private val buf = ByteArray(BLOCK_LEN)
    private var bufLen = 0
    private var t0 = 0L
    private var t1 = 0L

    init {
        require(outlen in 1..64) { "blake2b outlen must be 1..64" }
        h[0] = 0x6a09e667f3bcc908L xor 0x01010000L xor outlen.toLong()
        h[1] = 0xbb67ae8584caa73bUL.toLong()
        h[2] = 0x3c6ef372fe94f82bL
        h[3] = 0xa54ff53a5f1d36f1UL.toLong()
        h[4] = 0x510e527fade682d1L
        h[5] = 0x9b05688c2b3e6c1fUL.toLong()
        h[6] = 0x1f83d9abfb41bd6bL
        h[7] = 0x5be0cd19137e2179L
    }

    fun update(data: ByteArray, len: Int = data.size): Blake2b {
        var off = 0
        var remaining = len
        while (remaining > 0) {
            if (bufLen == BLOCK_LEN) { // 先把满缓冲刷出去
                t0 += BLOCK_LEN
                if (t0 == 0L) t1++
                compress(buf, false)
                bufLen = 0
            }
            val take = minOf(remaining, BLOCK_LEN - bufLen)
            System.arraycopy(data, off, buf, bufLen, take)
            bufLen += take
            off += take
            remaining -= take
        }
        return this
    }

    fun updateLe32(value: Int): Blake2b {
        val b = ByteArray(4)
        b[0] = (value and 0xFF).toByte()
        b[1] = ((value shr 8) and 0xFF).toByte()
        b[2] = ((value shr 16) and 0xFF).toByte()
        b[3] = ((value shr 24) and 0xFF).toByte()
        return update(b, 4)
    }

    fun final(): ByteArray {
        val out = ByteArray(outlen)
        t0 += bufLen
        if (t0 == 0L && bufLen > 0) t1++
        for (i in bufLen until BLOCK_LEN) buf[i] = 0
        compress(buf, last = true)
        for (i in 0 until outlen) out[i] = (h[i / 8] ushr (8 * (i % 8))).toByte()
        return out
    }

    private fun compress(block: ByteArray, last: Boolean) {
        val m = LongArray(16)
        for (i in 0 until 16) {
            var v = 0L
            for (j in 7 downTo 0) v = (v shl 8) or (block[8 * i + j].toLong() and 0xFF)
            m[i] = v
        }
        val v = LongArray(16)
        h.copyInto(v)   // RFC 7693：v[0..7] = 状态 h，v[8..15] = IV
        IV.copyInto(v, 8)
        v[12] = v[12] xor t0
        v[13] = v[13] xor t1
        if (last) v[14] = v[14].inv()

        for (r in 0 until 12) {
            val s = SIGMA[r % 10]
            g(v, 0, 4, 8, 12, m[s[0]], m[s[1]])
            g(v, 1, 5, 9, 13, m[s[2]], m[s[3]])
            g(v, 2, 6, 10, 14, m[s[4]], m[s[5]])
            g(v, 3, 7, 11, 15, m[s[6]], m[s[7]])
            g(v, 0, 5, 10, 15, m[s[8]], m[s[9]])
            g(v, 1, 6, 11, 12, m[s[10]], m[s[11]])
            g(v, 2, 7, 8, 13, m[s[12]], m[s[13]])
            g(v, 3, 4, 9, 14, m[s[14]], m[s[15]])
        }
        for (i in 0 until 8) h[i] = h[i] xor v[i] xor v[i + 8]
    }

    private fun g(v: LongArray, a: Int, b: Int, c: Int, d: Int, x: Long, y: Long) {
        v[a] = v[a] + v[b] + x
        v[d] = (v[d] xor v[a]).rotateRight(32)
        v[c] = v[c] + v[d]
        v[b] = (v[b] xor v[c]).rotateRight(24)
        v[a] = v[a] + v[b] + y
        v[d] = (v[d] xor v[a]).rotateRight(16)
        v[c] = v[c] + v[d]
        v[b] = (v[b] xor v[c]).rotateRight(63)
    }

    companion object {
        private const val BLOCK_LEN = 128
        private val IV = longArrayOf(
            0x6a09e667f3bcc908L, 0xbb67ae8584caa73bUL.toLong(),
            0x3c6ef372fe94f82bL, 0xa54ff53a5f1d36f1UL.toLong(),
            0x510e527fade682d1L, 0x9b05688c2b3e6c1fUL.toLong(),
            0x1f83d9abfb41bd6bL, 0x5be0cd19137e2179L,
        )
        private val SIGMA = arrayOf(
            intArrayOf(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
            intArrayOf(14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3),
            intArrayOf(11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4),
            intArrayOf(7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8),
            intArrayOf(9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13),
            intArrayOf(2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9),
            intArrayOf(12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11),
            intArrayOf(13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10),
            intArrayOf(6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5),
            intArrayOf(10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0),
        )
    }
}

/**
 * blake2b_long（blake2/blake2b.c）：可变长度哈希。
 * outLen<=64 时输出 Blake2b(outLen, LE32(outLen) || in)；否则按参考实现链式生成。
 */
internal fun blake2bLong(outLen: Int, input: ByteArray): ByteArray {
    val outLenBytes = ByteArray(4)
    outLenBytes[0] = (outLen and 0xFF).toByte()
    outLenBytes[1] = ((outLen shr 8) and 0xFF).toByte()
    outLenBytes[2] = ((outLen shr 16) and 0xFF).toByte()
    outLenBytes[3] = ((outLen shr 24) and 0xFF).toByte()

    if (outLen <= 64) {
        return Blake2b(outLen)
            .update(outLenBytes, 4)
            .update(input)
            .final()
    }
    var outBuffer = Blake2b(64)
        .update(outLenBytes, 4)
        .update(input)
        .final()
    val out = ByteArray(outLen)
    System.arraycopy(outBuffer, 0, out, 0, 32)
    var produced = 32
    var toProduce = outLen - 32
    val inBuffer = ByteArray(64)
    while (toProduce > 64) {
        System.arraycopy(outBuffer, 0, inBuffer, 0, 64)
        outBuffer = Blake2b(64).update(inBuffer, 64).final()
        System.arraycopy(outBuffer, 0, out, produced, 32)
        produced += 32
        toProduce -= 32
    }
    System.arraycopy(outBuffer, 0, inBuffer, 0, 64)
    outBuffer = Blake2b(toProduce).update(inBuffer, 64).final()
    System.arraycopy(outBuffer, 0, out, produced, toProduce)
    return out
}
