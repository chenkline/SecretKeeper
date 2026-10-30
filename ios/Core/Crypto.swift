import Foundation

/// iOS 端密码学原语门面（移动端原生实现，铁律二）。
///
/// 与桌面三端（mbedTLS + vendor/argon2）逐字节对齐的验证方式：
/// SecretCoreTests 读取仓库共享的 test-vectors/ 黄金向量并逐项断言。
///
/// 约定（与 src/include/core/crypto.h 相同）：
/// - Argon2id：m=10240KiB、t=3、p=1，输出 32 字节 KEK，盐随记录存储。
/// - AES-256-GCM：nonce 12 字节、tag 16 字节、AAD 分别为主密钥 ID / 机密 ID。
/// - RSA-OAEP-SHA256：MGF1 也用 SHA-256，label 为空，封装 32 字节数据密钥。
/// - Blake2b / Argon2id / AES-GCM / RSA 全部纯 Swift，无第三方依赖，
///   Linux(CI/本地验证) 与 macOS/iOS 编译同一份实现。
public enum Crypto {
    public static let argon2Version: UInt32 = 0x13
    public static let tagBits = 128
    public static let nonceLen = 12
    public static let tagLen = 16
    public static let wrappedLen = 256
    public static let maxOaepPlaintext = 190 // 256 - 2*32 - 2

    // KDF 参数与桌面端 crypto::kKdf* 常量一致（docs/02-crypto）。
    public static let kdfMemoryKiB = 10240
    public static let kdfIterations = 3
    public static let kdfParallelism = 1

    public static func randomBytes(_ n: Int) -> [UInt8] {
        var out = [UInt8](repeating: 0, count: n)
        for i in 0..<n { out[i] = UInt8.random(in: 0...255) }
        return out
    }

    /// 生成 16 字节随机 ID（对应桌面 crypto::generate_id）。
    public static func generateId() -> [UInt8] { randomBytes(16) }

    public static func hex(_ bytes: [UInt8]) -> String {
        let digits = Array("0123456789abcdef".utf8)
        var out = [UInt8]()
        out.reserveCapacity(bytes.count * 2)
        for b in bytes {
            out.append(digits[Int(b >> 4)])
            out.append(digits[Int(b & 0xF)])
        }
        return String(decoding: out, as: UTF8.self)
    }

    public static func unhex(_ s: String) -> [UInt8] {
        let t = s.trimmingCharacters(in: .whitespaces).lowercased()
        precondition(t.count % 2 == 0, "odd hex length")
        let chars = Array(t.utf8)
        var out = [UInt8]()
        out.reserveCapacity(t.count / 2)
        func digit(_ c: UInt8) -> UInt8 {
            switch c {
            case 0x30...0x39: return c - 0x30
            case 0x61...0x66: return c - 0x61 + 10
            default: fatalError("bad hex char")
            }
        }
        for i in stride(from: 0, to: chars.count, by: 2) {
            out.append((digit(chars[i]) << 4) | digit(chars[i + 1]))
        }
        return out
    }

    public static func constantEquals(_ a: [UInt8], _ b: [UInt8]) -> Bool {
        guard a.count == b.count else { return false }
        var diff: UInt8 = 0
        for i in 0..<a.count { diff |= a[i] ^ b[i] }
        return diff == 0
    }

    // ---- Argon2id（密码 → KEK）----

    /// Argon2id 密钥派生。参数与桌面端一致：m=10240KiB、t=3、p=1、输出 32 字节。
    public static func deriveKek(password: [UInt8], salt: [UInt8],
                                 memoryKiB: Int = 10240, iterations: Int = 3,
                                 parallelism: Int = 1, outputLen: Int = 32) -> [UInt8] {
        Argon2id.derive(password: password, salt: salt, memoryKiB: memoryKiB,
                        iterations: iterations, lanes: parallelism, outputLen: outputLen)
    }

    // ---- AES-256-GCM ----

    /// 返回 (密文, tag)；与桌面端容器布局一致，密文与 tag 分开保存。
    public static func aesGcmEncrypt(key: [UInt8], nonce: [UInt8], aad: [UInt8],
                                     plaintext: [UInt8]) throws -> ([UInt8], [UInt8]) {
        try AESGCM.encrypt(key: key, nonce: nonce, aad: aad, plaintext: plaintext)
    }

    /// 认证失败抛 GCMError.authenticationFailed；调用方按需求统一映射提示文案。
    public static func aesGcmDecrypt(key: [UInt8], nonce: [UInt8], aad: [UInt8],
                                     ciphertext: [UInt8], tag: [UInt8]) throws -> [UInt8] {
        try AESGCM.decrypt(key: key, nonce: nonce, aad: aad, ciphertext: ciphertext, tag: tag)
    }

    // ---- RSA-2048 OAEP-SHA256（MGF1-SHA256）----

    /// 生成主密钥密钥对。返回 (私钥 PKCS#8 DER, 公钥 SPKI DER)。
    public static func rsaGenerateKeyPair() throws -> (privateDER: [UInt8], publicDER: [UInt8]) {
        try RSA.generateKeyPair()
    }

    /// 封装数据密钥。明文超长抛 RsaError.plaintextTooLong。
    public static func rsaOaepEncrypt(publicSpki: [UInt8],
                                      plaintext: [UInt8]) throws -> [UInt8] {
        try RSA.oaepEncrypt(publicSPKI: publicSpki, plaintext: plaintext)
    }

    /// 解封装数据密钥。失败抛 RsaError.decryptionFailed。
    public static func rsaOaepDecrypt(privatePkcs8: [UInt8],
                                      wrapped: [UInt8]) throws -> [UInt8] {
        try RSA.oaepDecrypt(privatePKCS8: privatePkcs8, wrapped: wrapped)
    }
}
