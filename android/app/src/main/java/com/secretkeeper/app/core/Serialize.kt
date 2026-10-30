package com.secretkeeper.app.core

/**
 * 序列化层：字节流 <-> 内存对象。
 *
 * 实现 docs/03-data/数据格式与存储设计.md 定义的字节级格式，
 * 与桌面端 src/common/core/serialize.cpp 保持逐字节一致：
 * 全部整数大端序；变长字段一律 uint32 长度前缀 + 载荷。
 * 解析器对任何畸形输入都返回明确错误码，不做「尽力猜测」。
 *
 * 解析原则（文档 §3.4）：
 *   1. PayloadLen 必须与文件剩余长度精确一致，多余字节视为非法
 *   2. 每个 TLV 长度不得超过剩余可用长度
 *   3. 固定长度字段长度必须精确匹配
 */
object Serialize {
    const val ID_LENGTH = 16
    const val SALT_LENGTH = 16
    const val NONCE_LENGTH = 12
    const val TAG_LENGTH = 16

    val MASTER_KEY_MAGIC = byteArrayOf(0x53, 0x4D, 0x4B, 0x31) // "SMK1"
    val SECRET_MAGIC = byteArrayOf(0x53, 0x53, 0x43, 0x31) // "SSC1"

    const val FORMAT_VERSION = 0x0001
    const val HEADER_SIZE = 12 // Magic(4)+Version(2)+Flags(2)+PayloadLen(4)

    const val ALG_RSA2048 = 1L // 2 = SM2，v0.0.1 必须拒绝
    const val ENC_ALG_AES_GCM = 1L

    const val TLV_PREFIX_SIZE = 4

    /**
     * 任何单个 TLV 载荷与整个 payload 的合理上界（4 KiB）。
     * 从格式本身算出：主密钥文件 payload 实测 1642 字节、
     * 机密信息文件实测 368 字节，取 4 KiB 留出约 2.4 倍余量。
     * 用于识别被篡改的长度字段，并防止按畸形长度分配内存。
     */
    const val MAX_PLAUSIBLE_FILE_BYTES = 4L * 1024L

    /** 错误类别与 test-vectors/negative-cases.json 的 errorCategories 一一对应。 */
    enum class ParseError {
        OK,
        BAD_MAGIC,           // 文件类型不匹配
        UNSUPPORTED_VERSION, // 版本号超出支持范围
        TRUNCATED,           // 数据截断
        LENGTH_MISMATCH,     // 长度字段与实际不符
        UNKNOWN_ALGORITHM,   // 未知算法标识（如 v0.0.1 遇到 SM2）
        INVALID_UTF8,        // 名称或标题不是合法 UTF-8
    }

    /** 解析结果：成功携带对象，失败携带错误类别。 */
    sealed class Result<out T> {
        data class Ok<T>(val value: T) : Result<T>()
        data class Err(val error: ParseError) : Result<Nothing>()
    }

    /** 主密钥文件（.smk）。name 允许为空，仅用于显示，不参与加解密、不纳入 AAD。 */
    data class MasterKeyFile(
        val mkAlg: Long = ALG_RSA2048,
        val masterKeyId: ByteArray,
        val name: String, // 已校验的 UTF-8，可为空
        val salt: ByteArray, // 明文存储，这是跨端一致的前提
        val kdfMem: Long,
        val kdfIter: Long,
        val kdfPar: Long,
        val pubKeyEncAlg: Long = ENC_ALG_AES_GCM,
        val pubKeyNonce: ByteArray,
        val pubKeyTag: ByteArray,
        val pubKeyCipher: ByteArray,
        val privKeyEncAlg: Long = ENC_ALG_AES_GCM,
        val privKeyNonce: ByteArray,
        val privKeyTag: ByteArray,
        val privKeyCipher: ByteArray,
    )

    /** 机密信息文件（.ssc）。 */
    data class SecretFile(
        val secretId: ByteArray, // 明文，作为 GCM AAD
        val masterKeyId: ByteArray, // 明文，列表页无需解密即可比对
        val title: String, // UTF-8，可为空；不加密是明确的取舍
        val wrappedDk: ByteArray,
        val dataNonce: ByteArray,
        val dataTag: ByteArray,
        val dataCipher: ByteArray,
    )

    // ---------------------------------------------------------- 字节写出

    private fun putU8(out: MutableList<Byte>, v: Long) {
        out.add(v.toByte())
    }

    private fun putU16(out: MutableList<Byte>, v: Long) {
        out.add(((v shr 8) and 0xFF).toByte())
        out.add((v and 0xFF).toByte())
    }

    private fun putU32(out: MutableList<Byte>, v: Long) {
        out.add(((v shr 24) and 0xFF).toByte())
        out.add(((v shr 16) and 0xFF).toByte())
        out.add(((v shr 8) and 0xFF).toByte())
        out.add((v and 0xFF).toByte())
    }

    private fun putTlvBytes(out: MutableList<Byte>, data: ByteArray) {
        putU32(out, data.size.toLong())
        for (b in data) out.add(b)
    }

    private fun putTlvString(out: MutableList<Byte>, s: String) {
        val b = s.toByteArray(Charsets.UTF_8)
        putU32(out, b.size.toLong())
        for (x in b) out.add(x)
    }

    private fun putHeader(out: MutableList<Byte>, magic: ByteArray, payloadLen: Long) {
        for (m in magic) out.add(m)
        putU16(out, FORMAT_VERSION.toLong())
        putU16(out, 0) // Flags：v0.0.1 恒为 0
        putU32(out, payloadLen)
    }

    fun serializeMasterKey(f: MasterKeyFile): ByteArray {
        val p = mutableListOf<Byte>()
        putU8(p, f.mkAlg)
        putTlvBytes(p, f.masterKeyId)
        putTlvString(p, f.name)
        putTlvBytes(p, f.salt)
        putU32(p, f.kdfMem)
        putU32(p, f.kdfIter)
        putU32(p, f.kdfPar)
        putU8(p, f.pubKeyEncAlg)
        putTlvBytes(p, f.pubKeyNonce)
        putTlvBytes(p, f.pubKeyTag)
        putTlvBytes(p, f.pubKeyCipher)
        putU8(p, f.privKeyEncAlg)
        putTlvBytes(p, f.privKeyNonce)
        putTlvBytes(p, f.privKeyTag)
        putTlvBytes(p, f.privKeyCipher)
        val out = mutableListOf<Byte>()
        putHeader(out, MASTER_KEY_MAGIC, p.size.toLong())
        out.addAll(p)
        return out.toByteArray()
    }

    fun serializeSecret(f: SecretFile): ByteArray {
        val p = mutableListOf<Byte>()
        putTlvBytes(p, f.secretId)
        putTlvBytes(p, f.masterKeyId)
        putTlvString(p, f.title)
        putTlvBytes(p, f.wrappedDk)
        putTlvBytes(p, f.dataNonce)
        putTlvBytes(p, f.dataTag)
        putTlvBytes(p, f.dataCipher)
        val out = mutableListOf<Byte>()
        putHeader(out, SECRET_MAGIC, p.size.toLong())
        out.addAll(p)
        return out.toByteArray()
    }

    // ---------------------------------------------------------- 顺序读取

    /** TLV 读取状态，对应不同的错误类别（见 serialize.cpp Reader::TlvStatus）。 */
    private enum class TlvStatus { OK, NO_PREFIX, IMPLAUSIBLE, SHORT }

    private class Reader(private val data: ByteArray, private val start: Int) {
        var pos = start
            private set

        val consumed: Long
            get() = (pos - start).toLong()

        fun remaining(): Int = data.size - pos

        fun eof(): Boolean = pos >= data.size

        fun readU8(): Long? {
            if (remaining() < 1) return null
            return data[pos++].toUnsignedLong()
        }

        fun readU16(): Long? {
            if (remaining() < 2) return null
            val v = ((data[pos].toUnsignedLong() shl 8) or data[pos + 1].toUnsignedLong())
            pos += 2
            return v
        }

        fun readU32(): Long? {
            if (remaining() < 4) return null
            val v = (data[pos].toUnsignedLong() shl 24) or
                (data[pos + 1].toUnsignedLong() shl 16) or
                (data[pos + 2].toUnsignedLong() shl 8) or
                data[pos + 3].toUnsignedLong()
            pos += 4
            return v
        }

        fun readTlv(): Pair<ByteArray, TlvStatus> {
            if (remaining() < TLV_PREFIX_SIZE) return Pair(ByteArray(0), TlvStatus.NO_PREFIX)
            val len = readU32() ?: return Pair(ByteArray(0), TlvStatus.NO_PREFIX)
            // 长度字段可信度检查：超过合理上界即被篡改，同时防越界分配。
            if (len > MAX_PLAUSIBLE_FILE_BYTES) return Pair(ByteArray(0), TlvStatus.IMPLAUSIBLE)
            if (remaining() < len) return Pair(ByteArray(0), TlvStatus.SHORT)
            val out = data.copyOfRange(pos, pos + len.toInt())
            pos += len.toInt()
            return Pair(out, TlvStatus.OK)
        }

        /** 固定长度 TLV：长度必须精确匹配，不接受「读前 N 字节、忽略多余」。 */
        fun readTlvExact(expect: Int): Pair<ByteArray, TlvStatus> {
            if (remaining() < TLV_PREFIX_SIZE) return Pair(ByteArray(0), TlvStatus.NO_PREFIX)
            val len = readU32() ?: return Pair(ByteArray(0), TlvStatus.NO_PREFIX)
            if (len != expect.toLong()) return Pair(ByteArray(0), TlvStatus.IMPLAUSIBLE)
            if (remaining() < expect) return Pair(ByteArray(0), TlvStatus.SHORT)
            val out = data.copyOfRange(pos, pos + expect)
            pos += expect
            return Pair(out, TlvStatus.OK)
        }

        companion object {
            fun tlvError(s: TlvStatus): ParseError = when (s) {
                TlvStatus.OK -> ParseError.OK
                TlvStatus.NO_PREFIX -> ParseError.TRUNCATED
                TlvStatus.IMPLAUSIBLE -> ParseError.LENGTH_MISMATCH
                TlvStatus.SHORT -> ParseError.TRUNCATED
            }
        }
    }

    private fun Byte.toUnsignedLong(): Long = toLong() and 0xFFL

    // ---------------------------------------------------------- 容器头校验

    /**
     * 阶段 1：只校验头部固定部分。刻意不在这里比对 PayloadLen 的完整
     * 一致性——unknown-algorithm 必须在长度问题之前报出。
     */
    private fun checkHeaderPrefix(
        bytes: ByteArray,
        expectMagic: ByteArray,
    ): Pair<Long, ParseError> {
        if (bytes.size < HEADER_SIZE) return Pair(0, ParseError.TRUNCATED)
        for (i in expectMagic.indices) {
            if (bytes[i] != expectMagic[i]) return Pair(0, ParseError.BAD_MAGIC)
        }
        val r = Reader(bytes, 4)
        val version = r.readU16() ?: return Pair(0, ParseError.TRUNCATED)
        val flags = r.readU16() ?: return Pair(0, ParseError.TRUNCATED)
        val payloadLen = r.readU32() ?: return Pair(0, ParseError.TRUNCATED)
        if (version != FORMAT_VERSION.toLong()) return Pair(0, ParseError.UNSUPPORTED_VERSION)
        if (flags != 0L) return Pair(0, ParseError.LENGTH_MISMATCH)
        // PayloadLen 超过格式物理上界只能是被篡改的长度字段。
        if (payloadLen > MAX_PLAUSIBLE_FILE_BYTES) return Pair(0, ParseError.LENGTH_MISMATCH)
        return Pair(payloadLen, ParseError.OK)
    }

    /** 阶段 2：实际 > 声明 → 尾部多余字节 → length_mismatch；实际 < 声明交给 TLV 层判定。 */
    private fun checkPayloadLength(
        bytes: ByteArray,
        payloadLen: Long,
    ): ParseError =
        if (bytes.size - HEADER_SIZE > payloadLen) {
            ParseError.LENGTH_MISMATCH
        } else {
            ParseError.OK
        }

    // ---------------------------------------------------------- UTF-8 校验

    /**
     * 严格 UTF-8 校验：拒绝过长编码、代理区码点与超出 Unicode 范围的码点。
     * 通过校验后 String(bytes, UTF_8) 的往返是精确的。
     */
    private fun isValidUtf8(b: ByteArray): Boolean {
        var i = 0
        val n = b.size
        while (i < n) {
            val c = b[i].toInt() and 0xFF
            when {
                c < 0x80 -> i += 1
                (c and 0xE0) == 0xC0 -> {
                    if (i + 1 >= n || (b[i + 1].toInt() and 0xC0) != 0x80) return false
                    val cp = ((c and 0x1F) shl 6) or (b[i + 1].toInt() and 0x3F)
                    if (cp < 0x80) return false // 过长编码
                    i += 2
                }
                (c and 0xF0) == 0xE0 -> {
                    if (i + 2 >= n || (b[i + 1].toInt() and 0xC0) != 0x80 ||
                        (b[i + 2].toInt() and 0xC0) != 0x80
                    ) {
                        return false
                    }
                    val cp = ((c and 0x0F) shl 12) or
                        ((b[i + 1].toInt() and 0x3F) shl 6) or
                        (b[i + 2].toInt() and 0x3F)
                    if (cp < 0x800) return false // 过长编码
                    if (cp in 0xD800..0xDFFF) return false // 代理区码点
                    i += 3
                }
                (c and 0xF8) == 0xF0 -> {
                    if (i + 3 >= n || (b[i + 1].toInt() and 0xC0) != 0x80 ||
                        (b[i + 2].toInt() and 0xC0) != 0x80 ||
                        (b[i + 3].toInt() and 0xC0) != 0x80
                    ) {
                        return false
                    }
                    val cp = ((c and 0x07) shl 18) or
                        ((b[i + 1].toInt() and 0x3F) shl 12) or
                        ((b[i + 2].toInt() and 0x3F) shl 6) or
                        (b[i + 3].toInt() and 0x3F)
                    if (cp < 0x10000 || cp > 0x10FFFF) return false
                    i += 4
                }
                else -> return false
            }
        }
        return true
    }

    // ---------------------------------------------------------- 主密钥文件

    fun parseMasterKey(bytes: ByteArray): Result<MasterKeyFile> {
        val (payloadLen, headErr) = checkHeaderPrefix(bytes, MASTER_KEY_MAGIC)
        if (headErr != ParseError.OK) return Result.Err(headErr)
        val r = Reader(bytes, HEADER_SIZE)

        // mkAlg 是裸字节而非 TLV，可能因为 payload 为空而读不到。
        val mkAlg = r.readU8() ?: return Result.Err(ParseError.TRUNCATED)
        // v0.0.1 只支持 RSA-2048；SM2(2) 等必须明确拒绝。此检查刻意早于
        // 长度一致性检查：调用方最需要知道的是算法不支持。
        if (mkAlg != ALG_RSA2048) return Result.Err(ParseError.UNKNOWN_ALGORITHM)

        // 算法标识校验通过后再判断长度一致性。
        val lenErr = checkPayloadLength(bytes, payloadLen)
        if (lenErr != ParseError.OK) return Result.Err(lenErr)

        val (masterKeyId, st1) = r.readTlvExact(ID_LENGTH)
        if (st1 != TlvStatus.OK) return Result.Err(Reader.tlvError(st1))
        val (nameBytes, st2) = r.readTlv()
        if (st2 != TlvStatus.OK) return Result.Err(Reader.tlvError(st2))
        if (!isValidUtf8(nameBytes)) return Result.Err(ParseError.INVALID_UTF8)
        val name = String(nameBytes, Charsets.UTF_8)

        val (salt, st3) = r.readTlvExact(SALT_LENGTH)
        if (st3 != TlvStatus.OK) return Result.Err(Reader.tlvError(st3))

        val kdfMem = r.readU32() ?: return Result.Err(ParseError.TRUNCATED)
        val kdfIter = r.readU32() ?: return Result.Err(ParseError.TRUNCATED)
        val kdfPar = r.readU32() ?: return Result.Err(ParseError.TRUNCATED)

        val pubEncAlg = r.readU8() ?: return Result.Err(ParseError.TRUNCATED)
        if (pubEncAlg != ENC_ALG_AES_GCM) return Result.Err(ParseError.UNKNOWN_ALGORITHM)
        val (pubNonce, st4) = r.readTlvExact(NONCE_LENGTH)
        if (st4 != TlvStatus.OK) return Result.Err(Reader.tlvError(st4))
        val (pubTag, st5) = r.readTlvExact(TAG_LENGTH)
        if (st5 != TlvStatus.OK) return Result.Err(Reader.tlvError(st5))
        val (pubCipher, st6) = r.readTlv()
        if (st6 != TlvStatus.OK) return Result.Err(Reader.tlvError(st6))

        val privEncAlg = r.readU8() ?: return Result.Err(ParseError.TRUNCATED)
        if (privEncAlg != ENC_ALG_AES_GCM) return Result.Err(ParseError.UNKNOWN_ALGORITHM)
        val (privNonce, st7) = r.readTlvExact(NONCE_LENGTH)
        if (st7 != TlvStatus.OK) return Result.Err(Reader.tlvError(st7))
        val (privTag, st8) = r.readTlvExact(TAG_LENGTH)
        if (st8 != TlvStatus.OK) return Result.Err(Reader.tlvError(st8))
        val (privCipher, st9) = r.readTlv()
        if (st9 != TlvStatus.OK) return Result.Err(Reader.tlvError(st9))

        // 公私钥 nonce 必须不同：相同则两密文异或即可恢复两明文异或，
        // 加密彻底失效，必须拒绝。
        if (pubNonce.contentEquals(privNonce)) {
            return Result.Err(ParseError.LENGTH_MISMATCH)
        }

        // 解析成功后必须正好读完 payload，不允许有未消费的字节。
        if (!r.eof()) return Result.Err(ParseError.LENGTH_MISMATCH)
        return Result.Ok(
            MasterKeyFile(
                mkAlg = mkAlg,
                masterKeyId = masterKeyId,
                name = name,
                salt = salt,
                kdfMem = kdfMem,
                kdfIter = kdfIter,
                kdfPar = kdfPar,
                pubKeyEncAlg = pubEncAlg,
                pubKeyNonce = pubNonce,
                pubKeyTag = pubTag,
                pubKeyCipher = pubCipher,
                privKeyEncAlg = privEncAlg,
                privKeyNonce = privNonce,
                privKeyTag = privTag,
                privKeyCipher = privCipher,
            ),
        )
    }

    // -------------------------------------------------------- 机密信息文件

    fun parseSecret(bytes: ByteArray): Result<SecretFile> {
        val (payloadLen, headErr) = checkHeaderPrefix(bytes, SECRET_MAGIC)
        if (headErr != ParseError.OK) return Result.Err(headErr)
        // 机密信息文件没有算法标识字段，长度校验紧接头部校验。
        val lenErr = checkPayloadLength(bytes, payloadLen)
        if (lenErr != ParseError.OK) return Result.Err(lenErr)
        val r = Reader(bytes, HEADER_SIZE)

        val (secretId, st1) = r.readTlvExact(ID_LENGTH)
        if (st1 != TlvStatus.OK) return Result.Err(Reader.tlvError(st1))
        val (masterKeyId, st2) = r.readTlvExact(ID_LENGTH)
        if (st2 != TlvStatus.OK) return Result.Err(Reader.tlvError(st2))
        val (titleBytes, st3) = r.readTlv()
        if (st3 != TlvStatus.OK) return Result.Err(Reader.tlvError(st3))
        if (!isValidUtf8(titleBytes)) return Result.Err(ParseError.INVALID_UTF8)
        val title = String(titleBytes, Charsets.UTF_8)

        val (wrappedDk, st4) = r.readTlv()
        if (st4 != TlvStatus.OK) return Result.Err(Reader.tlvError(st4))
        val (dataNonce, st5) = r.readTlvExact(NONCE_LENGTH)
        if (st5 != TlvStatus.OK) return Result.Err(Reader.tlvError(st5))
        val (dataTag, st6) = r.readTlvExact(TAG_LENGTH)
        if (st6 != TlvStatus.OK) return Result.Err(Reader.tlvError(st6))
        val (dataCipher, st7) = r.readTlv()
        if (st7 != TlvStatus.OK) return Result.Err(Reader.tlvError(st7))

        if (!r.eof()) return Result.Err(ParseError.LENGTH_MISMATCH)
        return Result.Ok(
            SecretFile(
                secretId = secretId,
                masterKeyId = masterKeyId,
                title = title,
                wrappedDk = wrappedDk,
                dataNonce = dataNonce,
                dataTag = dataTag,
                dataCipher = dataCipher,
            ),
        )
    }
}
