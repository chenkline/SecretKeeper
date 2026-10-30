import Foundation

/// 序列化层：字节流 <-> 内存对象。
///
/// 实现 docs/03-data/数据格式与存储设计.md 定义的字节级格式，
/// 与桌面端 src/common/core/serialize.cpp 保持逐字节一致：
/// 全部整数大端序；变长字段一律 uint32 长度前缀 + 载荷。
/// 解析器对任何畸形输入都返回明确错误码，不做「尽力猜测」。
///
/// 移植基准：android/.../Serialize.kt（已通过 container-roundtrip 与
/// negative-cases 黄金向量）。
public enum Serialize {
    public static let idLength = 16
    public static let saltLength = 16
    public static let nonceLength = 12
    public static let tagLength = 16

    public static let masterKeyMagic: [UInt8] = [0x53, 0x4D, 0x4B, 0x31] // "SMK1"
    public static let secretMagic: [UInt8] = [0x53, 0x53, 0x43, 0x31] // "SSC1"

    public static let formatVersion: UInt16 = 0x0001
    public static let headerSize = 12 // Magic(4)+Version(2)+Flags(2)+PayloadLen(4)

    public static let algRsa2048: UInt8 = 1 // 2 = SM2，v0.0.1 必须拒绝
    public static let encAlgAesGcm: UInt8 = 1

    public static let tlvPrefixSize = 4

    /// 4 KiB 合理上界：识别被篡改的长度字段并防越界分配（见桌面 serialize.h 注释）。
    public static let maxPlausibleFileBytes: UInt32 = 4 * 1024

    /// 错误类别与 test-vectors/negative-cases.json 的 errorCategories 一一对应。
    public enum ParseError: Equatable {
        case ok
        case badMagic        // 文件类型不匹配
        case unsupportedVersion
        case truncated       // 数据截断
        case lengthMismatch  // 长度字段与实际不符
        case unknownAlgorithm
        case invalidUtf8

        public var wireName: String {
            switch self {
            case .ok: return "ok"
            case .badMagic: return "bad_magic"
            case .unsupportedVersion: return "unsupported_version"
            case .truncated: return "truncated"
            case .lengthMismatch: return "length_mismatch"
            case .unknownAlgorithm: return "unknown_algorithm"
            case .invalidUtf8: return "invalid_utf8"
            }
        }
    }

    /// 解析结果：成功携带对象，失败携带错误类别。
    public enum Result<T> {
        case ok(T)
        case err(ParseError)
    }

    // ---------------------------------------------------------- 数据结构

    /// 主密钥文件（.smk）。name 允许为空，仅用于显示，不参与加解密、不纳入 AAD。
    public struct MasterKeyFile: Equatable {
        public var mkAlg: UInt8
        public var masterKeyId: [UInt8]
        public var name: String // 已校验的 UTF-8，可为空
        public var salt: [UInt8] // 明文存储，这是跨端一致的前提
        public var kdfMem: UInt32
        public var kdfIter: UInt32
        public var kdfPar: UInt32
        public var pubKeyEncAlg: UInt8
        public var pubKeyNonce: [UInt8]
        public var pubKeyTag: [UInt8]
        public var pubKeyCipher: [UInt8] // 布局：密文 + 16 字节 tag 尾（见 service 层）
        public var privKeyEncAlg: UInt8
        public var privKeyNonce: [UInt8]
        public var privKeyTag: [UInt8]
        public var privKeyCipher: [UInt8]

        public init(mkAlg: UInt8 = Serialize.algRsa2048,
                    masterKeyId: [UInt8], name: String, salt: [UInt8],
                    kdfMem: UInt32, kdfIter: UInt32, kdfPar: UInt32,
                    pubKeyEncAlg: UInt8 = Serialize.encAlgAesGcm,
                    pubKeyNonce: [UInt8], pubKeyTag: [UInt8], pubKeyCipher: [UInt8],
                    privKeyEncAlg: UInt8 = Serialize.encAlgAesGcm,
                    privKeyNonce: [UInt8], privKeyTag: [UInt8], privKeyCipher: [UInt8]) {
            self.mkAlg = mkAlg
            self.masterKeyId = masterKeyId
            self.name = name
            self.salt = salt
            self.kdfMem = kdfMem
            self.kdfIter = kdfIter
            self.kdfPar = kdfPar
            self.pubKeyEncAlg = pubKeyEncAlg
            self.pubKeyNonce = pubKeyNonce
            self.pubKeyTag = pubKeyTag
            self.pubKeyCipher = pubKeyCipher
            self.privKeyEncAlg = privKeyEncAlg
            self.privKeyNonce = privKeyNonce
            self.privKeyTag = privKeyTag
            self.privKeyCipher = privKeyCipher
        }
    }

    /// 机密信息文件（.ssc）。
    public struct SecretFile: Equatable {
        public var secretId: [UInt8] // 明文，作为 GCM AAD
        public var masterKeyId: [UInt8] // 明文，列表页无需解密即可比对
        public var title: String // UTF-8，可为空；不加密是明确的取舍
        public var wrappedDk: [UInt8]
        public var dataNonce: [UInt8]
        public var dataTag: [UInt8]
        public var dataCipher: [UInt8] // 布局：密文 + 16 字节 tag 尾

        public init(secretId: [UInt8], masterKeyId: [UInt8], title: String,
                    wrappedDk: [UInt8], dataNonce: [UInt8], dataTag: [UInt8],
                    dataCipher: [UInt8]) {
            self.secretId = secretId
            self.masterKeyId = masterKeyId
            self.title = title
            self.wrappedDk = wrappedDk
            self.dataNonce = dataNonce
            self.dataTag = dataTag
            self.dataCipher = dataCipher
        }
    }

    // ---------------------------------------------------------- 序列化

    public static func serializeMasterKey(_ f: MasterKeyFile) -> [UInt8] {
        var p = [UInt8]()
        putU8(&p, f.mkAlg)
        putTlvBytes(&p, f.masterKeyId)
        putTlvString(&p, f.name)
        putTlvBytes(&p, f.salt)
        putU32(&p, f.kdfMem)
        putU32(&p, f.kdfIter)
        putU32(&p, f.kdfPar)
        putU8(&p, f.pubKeyEncAlg)
        putTlvBytes(&p, f.pubKeyNonce)
        putTlvBytes(&p, f.pubKeyTag)
        putTlvBytes(&p, f.pubKeyCipher)
        putU8(&p, f.privKeyEncAlg)
        putTlvBytes(&p, f.privKeyNonce)
        putTlvBytes(&p, f.privKeyTag)
        putTlvBytes(&p, f.privKeyCipher)
        var out = [UInt8]()
        putHeader(&out, masterKeyMagic, p.count)
        out.append(contentsOf: p)
        return out
    }

    public static func serializeSecret(_ f: SecretFile) -> [UInt8] {
        var p = [UInt8]()
        putTlvBytes(&p, f.secretId)
        putTlvBytes(&p, f.masterKeyId)
        putTlvString(&p, f.title)
        putTlvBytes(&p, f.wrappedDk)
        putTlvBytes(&p, f.dataNonce)
        putTlvBytes(&p, f.dataTag)
        putTlvBytes(&p, f.dataCipher)
        var out = [UInt8]()
        putHeader(&out, secretMagic, p.count)
        out.append(contentsOf: p)
        return out
    }

    // ---------------------------------------------------------- 顺序读取

    /// TLV 读取状态，对应不同的错误类别（见 serialize.cpp Reader::TlvStatus）。
    private enum TlvStatus {
        case ok
        case noPrefix
        case implausible
        case short

        var error: ParseError {
            switch self {
            case .ok: return .ok
            case .noPrefix: return .truncated
            case .implausible: return .lengthMismatch
            case .short: return .truncated
            }
        }
    }

    private struct Reader {
        let data: [UInt8]
        var pos: Int

        init(_ data: [UInt8], _ start: Int) {
            self.data = data
            self.pos = start
        }

        var eof: Bool { pos >= data.count }

        mutating func readU8() -> UInt8? {
            guard pos < data.count else { return nil }
            defer { pos += 1 }
            return data[pos]
        }

        mutating func readU16() -> UInt16? {
            guard pos + 2 <= data.count else { return nil }
            defer { pos += 2 }
            return (UInt16(data[pos]) << 8) | UInt16(data[pos + 1])
        }

        mutating func readU32() -> UInt32? {
            guard pos + 4 <= data.count else { return nil }
            defer { pos += 4 }
            return (UInt32(data[pos]) << 24) | (UInt32(data[pos + 1]) << 16)
                | (UInt32(data[pos + 2]) << 8) | UInt32(data[pos + 3])
        }

        mutating func readTlv() -> ([UInt8], TlvStatus) {
            guard data.count - pos >= tlvPrefixSize else { return ([], .noPrefix) }
            guard let len = readU32() else { return ([], .noPrefix) }
            // 长度字段可信度检查：超过合理上界即被篡改，同时防越界分配。
            if len > maxPlausibleFileBytes { return ([], .implausible) }
            guard data.count - pos >= Int(len) else { return ([], .short) }
            let out = Array(data[pos..<(pos + Int(len))])
            pos += Int(len)
            return (out, .ok)
        }

        /// 固定长度 TLV：长度必须精确匹配，不接受「读前 N 字节、忽略多余」。
        mutating func readTlvExact(_ expect: Int) -> ([UInt8], TlvStatus) {
            guard data.count - pos >= tlvPrefixSize else { return ([], .noPrefix) }
            guard let len = readU32() else { return ([], .noPrefix) }
            if len != UInt32(expect) { return ([], .implausible) }
            guard data.count - pos >= expect else { return ([], .short) }
            let out = Array(data[pos..<(pos + expect)])
            pos += expect
            return (out, .ok)
        }
    }

    // ---------------------------------------------------------- 容器头校验

    /// 阶段 1：只校验头部固定部分。unknown-algorithm 必须在长度问题之前报出。
    private static func checkHeaderPrefix(_ bytes: [UInt8],
                                          _ expectMagic: [UInt8]) -> (UInt32, ParseError) {
        guard bytes.count >= headerSize else { return (0, .truncated) }
        for i in 0..<expectMagic.count where bytes[i] != expectMagic[i] {
            return (0, .badMagic)
        }
        var r = Reader(bytes, 4)
        guard let version = r.readU16(), let flags = r.readU16(),
              let payloadLen = r.readU32() else { return (0, .truncated) }
        if version != formatVersion { return (0, .unsupportedVersion) }
        if flags != 0 { return (0, .lengthMismatch) }
        // PayloadLen 超过格式物理上界只能是被篡改的长度字段。
        if payloadLen > maxPlausibleFileBytes { return (0, .lengthMismatch) }
        return (payloadLen, .ok)
    }

    /// 阶段 2：实际 > 声明 → 尾部多余字节 → length_mismatch；实际 < 声明交给 TLV 层判定。
    private static func checkPayloadLength(_ bytes: [UInt8], _ payloadLen: UInt32) -> ParseError {
        bytes.count - headerSize > Int(payloadLen) ? .lengthMismatch : .ok
    }

    // ---------------------------------------------------------- UTF-8 校验

    /// 严格 UTF-8 校验：拒绝过长编码、代理区码点与超出 Unicode 范围的码点。
    private static func isValidUtf8(_ b: [UInt8]) -> Bool {
        var i = 0
        let n = b.count
        while i < n {
            let c = b[i]
            if c < 0x80 {
                i += 1
            } else if c & 0xE0 == 0xC0 {
                guard i + 1 < n, b[i + 1] & 0xC0 == 0x80 else { return false }
                let cp = (UInt32(c & 0x1F) << 6) | UInt32(b[i + 1] & 0x3F)
                if cp < 0x80 { return false } // 过长编码
                i += 2
            } else if c & 0xF0 == 0xE0 {
                guard i + 2 < n, b[i + 1] & 0xC0 == 0x80, b[i + 2] & 0xC0 == 0x80 else {
                    return false
                }
                let cp = (UInt32(c & 0x0F) << 12) | (UInt32(b[i + 1] & 0x3F) << 6)
                    | UInt32(b[i + 2] & 0x3F)
                if cp < 0x800 { return false } // 过长编码
                if cp >= 0xD800 && cp <= 0xDFFF { return false } // 代理区码点
                i += 3
            } else if c & 0xF8 == 0xF0 {
                guard i + 3 < n, b[i + 1] & 0xC0 == 0x80,
                      b[i + 2] & 0xC0 == 0x80, b[i + 3] & 0xC0 == 0x80 else {
                    return false
                }
                let cp = (UInt32(c & 0x07) << 18) | (UInt32(b[i + 1] & 0x3F) << 12)
                    | (UInt32(b[i + 2] & 0x3F) << 6) | UInt32(b[i + 3] & 0x3F)
                if cp < 0x10000 || cp > 0x10FFFF { return false }
                i += 4
            } else {
                return false
            }
        }
        return true
    }

    // ---------------------------------------------------------- 主密钥文件

    public static func parseMasterKey(_ bytes: [UInt8]) -> Result<MasterKeyFile> {
        let (payloadLen, headErr) = checkHeaderPrefix(bytes, masterKeyMagic)
        if headErr != .ok { return .err(headErr) }
        var r = Reader(bytes, headerSize)

        // mkAlg 是裸字节而非 TLV，可能因为 payload 为空而读不到。
        guard let mkAlg = r.readU8() else { return .err(.truncated) }
        // v0.0.1 只支持 RSA-2048；SM2(2) 等必须明确拒绝。此检查刻意早于长度校验。
        if mkAlg != algRsa2048 { return .err(.unknownAlgorithm) }

        // 算法标识校验通过后再判断长度一致性。
        let lenErr = checkPayloadLength(bytes, payloadLen)
        if lenErr != .ok { return .err(lenErr) }

        let (masterKeyId, st1) = r.readTlvExact(idLength)
        if st1 != .ok { return .err(st1.error) }
        let (nameBytes, st2) = r.readTlv()
        if st2 != .ok { return .err(st2.error) }
        if !isValidUtf8(nameBytes) { return .err(.invalidUtf8) }
        let name = String(decoding: nameBytes, as: UTF8.self)

        let (salt, st3) = r.readTlvExact(saltLength)
        if st3 != .ok { return .err(st3.error) }

        guard let kdfMem = r.readU32(), let kdfIter = r.readU32(),
              let kdfPar = r.readU32() else { return .err(.truncated) }

        guard let pubEncAlg = r.readU8() else { return .err(.truncated) }
        if pubEncAlg != encAlgAesGcm { return .err(.unknownAlgorithm) }
        let (pubNonce, st4) = r.readTlvExact(nonceLength)
        if st4 != .ok { return .err(st4.error) }
        let (pubTag, st5) = r.readTlvExact(tagLength)
        if st5 != .ok { return .err(st5.error) }
        let (pubCipher, st6) = r.readTlv()
        if st6 != .ok { return .err(st6.error) }

        guard let privEncAlg = r.readU8() else { return .err(.truncated) }
        if privEncAlg != encAlgAesGcm { return .err(.unknownAlgorithm) }
        let (privNonce, st7) = r.readTlvExact(nonceLength)
        if st7 != .ok { return .err(st7.error) }
        let (privTag, st8) = r.readTlvExact(tagLength)
        if st8 != .ok { return .err(st8.error) }
        let (privCipher, st9) = r.readTlv()
        if st9 != .ok { return .err(st9.error) }

        // 公私钥 nonce 必须不同：相同则两密文异或即可恢复两明文异或。
        if pubNonce == privNonce { return .err(.lengthMismatch) }

        // 解析成功后必须正好读完 payload。
        if !r.eof { return .err(.lengthMismatch) }
        return .ok(MasterKeyFile(
            mkAlg: mkAlg, masterKeyId: masterKeyId, name: name, salt: salt,
            kdfMem: kdfMem, kdfIter: kdfIter, kdfPar: kdfPar,
            pubKeyEncAlg: pubEncAlg, pubKeyNonce: pubNonce, pubKeyTag: pubTag,
            pubKeyCipher: pubCipher,
            privKeyEncAlg: privEncAlg, privKeyNonce: privNonce, privKeyTag: privTag,
            privKeyCipher: privCipher
        ))
    }

    // -------------------------------------------------------- 机密信息文件

    public static func parseSecret(_ bytes: [UInt8]) -> Result<SecretFile> {
        let (payloadLen, headErr) = checkHeaderPrefix(bytes, secretMagic)
        if headErr != .ok { return .err(headErr) }
        // 机密信息文件没有算法标识字段，长度校验紧接头部校验。
        let lenErr = checkPayloadLength(bytes, payloadLen)
        if lenErr != .ok { return .err(lenErr) }
        var r = Reader(bytes, headerSize)

        let (secretId, st1) = r.readTlvExact(idLength)
        if st1 != .ok { return .err(st1.error) }
        let (masterKeyId, st2) = r.readTlvExact(idLength)
        if st2 != .ok { return .err(st2.error) }
        let (titleBytes, st3) = r.readTlv()
        if st3 != .ok { return .err(st3.error) }
        if !isValidUtf8(titleBytes) { return .err(.invalidUtf8) }
        let title = String(decoding: titleBytes, as: UTF8.self)

        let (wrappedDk, st4) = r.readTlv()
        if st4 != .ok { return .err(st4.error) }
        let (dataNonce, st5) = r.readTlvExact(nonceLength)
        if st5 != .ok { return .err(st5.error) }
        let (dataTag, st6) = r.readTlvExact(tagLength)
        if st6 != .ok { return .err(st6.error) }
        let (dataCipher, st7) = r.readTlv()
        if st7 != .ok { return .err(st7.error) }

        if !r.eof { return .err(.lengthMismatch) }
        return .ok(SecretFile(
            secretId: secretId, masterKeyId: masterKeyId, title: title,
            wrappedDk: wrappedDk, dataNonce: dataNonce, dataTag: dataTag,
            dataCipher: dataCipher
        ))
    }

    // ---------------------------------------------------------- 字节写出

    private static func putU8(_ out: inout [UInt8], _ v: UInt8) { out.append(v) }

    private static func putU16(_ out: inout [UInt8], _ v: UInt16) {
        out.append(UInt8((v >> 8) & 0xFF))
        out.append(UInt8(v & 0xFF))
    }

    private static func putU32(_ out: inout [UInt8], _ v: UInt32) {
        out.append(UInt8((v >> 24) & 0xFF))
        out.append(UInt8((v >> 16) & 0xFF))
        out.append(UInt8((v >> 8) & 0xFF))
        out.append(UInt8(v & 0xFF))
    }

    private static func putTlvBytes(_ out: inout [UInt8], _ data: [UInt8]) {
        putU32(&out, UInt32(data.count))
        out.append(contentsOf: data)
    }

    private static func putTlvString(_ out: inout [UInt8], _ s: String) {
        let b = Array(s.utf8)
        putU32(&out, UInt32(b.count))
        out.append(contentsOf: b)
    }

    private static func putHeader(_ out: inout [UInt8], _ magic: [UInt8], _ payloadLen: Int) {
        out.append(contentsOf: magic)
        putU16(&out, formatVersion)
        putU16(&out, 0) // Flags：v0.0.1 恒为 0
        putU32(&out, UInt32(payloadLen))
    }
}
