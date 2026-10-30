import Foundation

/// RSA-2048 + OAEP-SHA256（MGF1-SHA256，label 为空），纯 Swift。
/// 密钥编码为标准 PKCS#8（私钥）/ SPKI（公钥）DER，与桌面 mbedTLS、
/// Android java.security 的输出互通（黄金向量 rsa-oaep.json 的固定
/// 密钥对用于钉死解析兼容性）。
enum RsaError: Error, Equatable {
    case invalidKey
    case plaintextTooLong
    case decryptionFailed // 解封装失败（OAEP 编码校验不过 / 密文超长）
}

enum RSA {
    static let modulusBits = 2048
    static let modulusBytes = modulusBits / 8 // 256
    static let maxOaepPlaintext = modulusBytes - 2 * 32 - 2 // 190
    static let publicExponent: UInt64 = 65537

    // ------------------------------------------------------ 密钥对生成

    /// 生成 RSA-2048 密钥对，返回 (私钥 PKCS#8 DER, 公钥 SPKI DER)。
    /// d = e^-1 mod lcm(p-1, q-1)（与 mbedTLS 生成方式一致）。
    static func generateKeyPair() throws -> (privateDER: [UInt8], publicDER: [UInt8]) {
        let e = BigUInt(limbs: [UInt32(publicExponent), 0])
        var rng = SystemRandomNumberGenerator()
        let p = try generatePrime(bits: modulusBits / 2, e: e, &rng)
        var q = try generatePrime(bits: modulusBits / 2, e: e, &rng)
        // 保证 p != q
        while BigUInt.compare(p, q) == 0 {
            q = try generatePrime(bits: modulusBits / 2, e: e, &rng)
        }
        let n = BigUInt.mul(p, q)
        precondition(n.bitCount == modulusBits, "模长必须是完整 2048 位")

        let p1 = BigUInt.sub(p, BigUInt(limbs: [1]))
        let q1 = BigUInt.sub(q, BigUInt(limbs: [1]))
        // lcm(p-1, q-1) = (p-1)(q-1)/gcd
        let g = BigUInt.gcd(p1, q1)
        let phi = BigUInt.mul(p1, q1)
        let lambda = BigUInt.divMod(phi, g).0
        let d = BigUInt.modInverse(e, lambda)
        precondition(!d.isZero, "e 与 λ 互素")

        let dp = BigUInt.divMod(d, p1).1
        let dq = BigUInt.divMod(d, q1).1
        let qInv = BigUInt.modInverse(q, p) // q^{-1} mod p（mbedTLS 同义）

        let privDER = pkcs8Encode(n: n, e: e, d: d, p: p, q: q,
                                  dp: dp, dq: dq, qInv: qInv)
        let pubDER = spkiEncode(n: n, e: e)
        return (privDER, pubDER)
    }

    private static func generatePrime(bits: Int, e: BigUInt,
                                      _ rng: inout SystemRandomNumberGenerator) throws -> BigUInt {
        // 小素数试除表（拒绝掉 ~86% 的随机候选，Miller-Rabin 只跑幸存者）
        let smallPrimes = sievePrimes(below: 10000)
        for _ in 0..<200_000 {
            var candidate = randomOdd(bits: bits, &rng)
            // 试除
            var ok = true
            for sp in smallPrimes {
                let m = BigUInt(limbs: [UInt32(sp), 0])
                if BigUInt.compare(candidate, m) <= 0 { break }
                if BigUInt.mod(candidate, m).isZero { ok = false; break }
            }
            if !ok { continue }
            // gcd(p-1, e) 必须为 1，否则 d 不存在
            let p1 = BigUInt.sub(candidate, BigUInt(limbs: [1]))
            if BigUInt.compare(BigUInt.gcd(p1, e), BigUInt(limbs: [1])) != 0 {
                continue
            }
            if millerRabin(candidate, rounds: 12, &rng) {
                return candidate
            }
        }
        throw RsaError.invalidKey
    }

    /// 置顶两位的随机奇数（保证两素数乘积占满 2048 位）
    private static func randomOdd(bits: Int, _ rng: inout SystemRandomNumberGenerator) -> BigUInt {
        var bytes = [UInt8](repeating: 0, count: bits / 8)
        for i in 0..<bytes.count { bytes[i] = UInt8.random(in: 0...255, using: &rng) }
        bytes[0] |= 0xC0 // 顶两位置 1
        bytes[bytes.count - 1] |= 0x01 // 奇数
        return BigUInt.fromBytesBE(bytes)
    }

    private static func sievePrimes(below limit: Int) -> [Int] {
        var flags = [Bool](repeating: true, count: limit)
        var out = [Int]()
        for i in 2..<limit {
            if flags[i] {
                out.append(i)
                var j = i * i
                while j < limit { flags[j] = false; j += i }
            }
        }
        return out
    }

    private static func millerRabin(_ n: BigUInt, rounds: Int,
                                    _ rng: inout SystemRandomNumberGenerator) -> Bool {
        // n 为奇数（调用方保证）
        let n1 = BigUInt.sub(n, BigUInt(limbs: [1]))
        var r = 0
        var d = n1
        while !d.isZero && d.limbs[0] & 1 == 0 {
            d = BigUInt.divMod(d, BigUInt(limbs: [2])).0
            r += 1
        }
        for _ in 0..<rounds {
            // 随机 a ∈ [2, n-2]
            var a = BigUInt.zero
            while BigUInt.compare(a, BigUInt(limbs: [2])) < 0
                || BigUInt.compare(a, n1) >= 0 || a.isZero {
                var bytes = [UInt8](repeating: 0, count: max(4, (n.bitCount + 7) / 8))
                for i in 0..<bytes.count { bytes[i] = UInt8.random(in: 0...255, using: &rng) }
                a = BigUInt.fromBytesBE(bytes)
            }
            var x = BigUInt.modPow(a, d, n)
            if BigUInt.compare(x, BigUInt(limbs: [1])) == 0
                || BigUInt.compare(x, n1) == 0 { continue }
            var witness = true
            for _ in 0..<(r - 1 < 0 ? 0 : r - 1) {
                x = BigUInt.mod(BigUInt.mul(x, x), n)
                if BigUInt.compare(x, n1) == 0 { witness = false; break }
                if x.isZero { break }
            }
            if witness { return false }
        }
        return true
    }

    // ------------------------------------------------------ OAEP-SHA256

    /// 封装数据密钥：RSA-OAEP-SHA256，MGF1 同为 SHA-256，label 为空。
    static func oaepEncrypt(publicSPKI: [UInt8], plaintext: [UInt8]) throws -> [UInt8] {
        guard plaintext.count <= maxOaepPlaintext else { throw RsaError.plaintextTooLong }
        let key = try parseSPKI(publicSPKI)
        // EMSA-OAEP 编码（RFC 8017 §7.1.1，label 空，hLen=32）
        let hLen = 32
        let lHash = sha256([]) // 空标签的哈希（RFC 8017 §7.1）
        let ps = [UInt8](repeating: 0, count: modulusBytes - plaintext.count - 2 * hLen - 2)
        var db = [UInt8](repeating: 0, count: modulusBytes - hLen - 1)
        // DB 布局：lHash | PS(全 0) | 0x01 | M
        db[0..<hLen] = lHash[0...]
        db[hLen..<(hLen + ps.count)] = ps[0...]
        db[hLen + ps.count] = 0x01 // 分隔符紧跟 PS，M 从下一字节起
        db[(hLen + ps.count + 1)...] = plaintext[0...]
        var seed = [UInt8](repeating: 0, count: hLen)
        for i in 0..<hLen { seed[i] = UInt8.random(in: 0...255) }
        let dbMask = mgf1Sha256(seed, db.count)
        for i in 0..<db.count { db[i] ^= dbMask[i] }
        let seedMask = mgf1Sha256(db, hLen)
        var masked = seed
        for i in 0..<hLen { masked[i] ^= seedMask[i] }
        var em = [UInt8](repeating: 0, count: modulusBytes)
        em[0] = 0x00
        em[1..<(1 + hLen)] = masked[0...]
        em[(1 + hLen)...] = db[0...]

        let c = BigUInt.modPow(BigUInt.fromBytesBE(em), key.exponent, key.modulus)
        return c.bytesBE(modulusBytes)
    }

    /// 解封装数据密钥。编码校验失败抛 decryptionFailed（对应 wrong_password 提示）。
    static func oaepDecrypt(privatePKCS8: [UInt8], wrapped: [UInt8]) throws -> [UInt8] {
        guard wrapped.count == modulusBytes else { throw RsaError.decryptionFailed }
        let key = try parsePKCS8(privatePKCS8)
        let c = BigUInt.fromBytesBE(wrapped)
        if BigUInt.compare(c, key.modulus) >= 0 { throw RsaError.decryptionFailed }
        let em = BigUInt.modPow(c, key.exponent, key.modulus).bytesBE(modulusBytes)

        // EMSA-OAEP 解码（恒定时间倾向：全程不提前返回成功路径）
        let hLen = 32
        if em[0] != 0x00 { throw RsaError.decryptionFailed }
        var maskedSeed = Array(em[1..<(1 + hLen)])
        let maskedDB = Array(em[(1 + hLen)...])
        let seedMask = mgf1Sha256(maskedDB, hLen)
        for i in 0..<hLen { maskedSeed[i] ^= seedMask[i] }
        let dbMask = mgf1Sha256(maskedSeed, maskedDB.count)
        var db = maskedDB
        for i in 0..<db.count { db[i] ^= dbMask[i] }
        // DB 布局：lHash | PS(全 0) | 0x01 | M
        let lHash = sha256([])
        if Array(db[0..<hLen]) != lHash { throw RsaError.decryptionFailed }
        var msgStart = -1
        for i in hLen..<db.count {
            if db[i] == 0x01 { msgStart = i + 1; break }
            if db[i] != 0x00 { throw RsaError.decryptionFailed }
        }
        if msgStart < 0 || msgStart > db.count { throw RsaError.decryptionFailed }
        return Array(db[msgStart...])
    }

    // ------------------------------------------------------ 密钥结构

    struct ParsedKey {
        var modulus: BigUInt
        var exponent: BigUInt
    }

    // ------------------------------------------------------ DER 编解码

    // 整数 DER：tag 0x02 + 长度 + 大端内容（高位 0x00 消歧）
    private static func derInteger(_ v: BigUInt) -> [UInt8] {
        var content = v.bytesBE((v.bitCount + 8) / 8) // 多留 1 字节给符号位
        if content.isEmpty { content = [0] }
        if content[0] & 0x80 != 0 { content.insert(0x00, at: 0) }
        // 去多余前导零（保留一个）
        while content.count > 1 && content[0] == 0x00 && content[1] & 0x80 == 0 {
            content.removeFirst()
        }
        return [0x02] + derLength(content.count) + content
    }

    private static func derIntegerBytes(_ bytes: [UInt8]) -> [UInt8] {
        var content = bytes
        if content.isEmpty { content = [0] }
        if content[0] & 0x80 != 0 { content.insert(0x00, at: 0) }
        while content.count > 1 && content[0] == 0x00 && content[1] & 0x80 == 0 {
            content.removeFirst()
        }
        return [0x02] + derLength(content.count) + content
    }

    private static func derLength(_ len: Int) -> [UInt8] {
        if len < 0x80 { return [UInt8(len)] }
        var bytes = [UInt8]()
        var v = len
        while v > 0 { bytes.insert(UInt8(v & 0xFF), at: 0); v >>= 8 }
        return [UInt8(0x80 | bytes.count)] + bytes
    }

    private static func derSequence(_ content: [UInt8]) -> [UInt8] {
        [0x30] + derLength(content.count) + content
    }

    /// PKCS#8 PrivateKeyInfo ::= SEQUENCE { version 0, AlgorithmIdentifier, OCTET STRING }
    private static func pkcs8Encode(n: BigUInt, e: BigUInt, d: BigUInt, p: BigUInt,
                                    q: BigUInt, dp: BigUInt, dq: BigUInt,
                                    qInv: BigUInt) -> [UInt8] {
        // PKCS#1 RSAPrivateKey
        let inner = derInteger(BigUInt(limbs: [0]))
            + derInteger(n) + derInteger(e) + derInteger(d)
            + derInteger(p) + derInteger(q) + derInteger(dp) + derInteger(dq)
            + derInteger(qInv)
        let pkcs1 = derSequence(inner)
        // AlgorithmIdentifier: SEQUENCE { OID 1.2.840.113549.1.1.1, NULL }
        let algId = derSequence([0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01,
                                 0x05, 0x00])
        let octet = [0x04] + derLength(pkcs1.count) + pkcs1
        return derSequence(derInteger(BigUInt(limbs: [0])) + algId + octet)
    }

    /// SPKI ::= SEQUENCE { AlgorithmIdentifier, BIT STRING { RSAPublicKey } }
    private static func spkiEncode(n: BigUInt, e: BigUInt) -> [UInt8] {
        let inner = derInteger(n) + derInteger(e)
        let pkcs1 = derSequence(inner)
        let algId = derSequence([0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01,
                                 0x05, 0x00])
        let bitString = [0x03] + derLength(pkcs1.count + 1) + [0x00] + pkcs1
        return derSequence(algId + bitString)
    }

    // 解析辅助：返回 (tag, content, 剩余)
    private static func derRead(_ data: [UInt8], _ offset: Int) throws -> (UInt8, [UInt8], Int) {
        guard offset + 2 <= data.count else { throw RsaError.invalidKey }
        let tag = data[offset]
        var pos = offset + 1
        var len = Int(data[pos]); pos += 1
        if len & 0x80 != 0 {
            let numBytes = len & 0x7F
            guard numBytes <= 4, pos + numBytes <= data.count else { throw RsaError.invalidKey }
            len = 0
            for _ in 0..<numBytes { len = (len << 8) | Int(data[pos]); pos += 1 }
        }
        guard pos + len <= data.count else { throw RsaError.invalidKey }
        return (tag, Array(data[pos..<(pos + len)]), pos + len)
    }

    private static func derParseInteger(_ content: [UInt8]) throws -> BigUInt {
        guard !content.isEmpty else { throw RsaError.invalidKey }
        var v = content
        while v.count > 1 && v[0] == 0 { v.removeFirst() } // 去符号前导零
        return BigUInt.fromBytesBE(v)
    }

    /// 解析 PKCS#8 私钥（也直接接受裸 PKCS#1 序列）。
    static func parsePKCS8(_ der: [UInt8]) throws -> ParsedKey {
        let (tag, seq, _) = try derRead(der, 0)
        guard tag == 0x30 else { throw RsaError.invalidKey }
        // 尝试按 PKCS#8 解
        var pos = 0
        var found: ParsedKey?
        do {
            let (t0, c0, next) = try derRead(seq, pos) // version
            guard t0 == 0x02 else { throw RsaError.invalidKey }
            _ = try derParseInteger(c0)
            let (t1, alg, next1) = try derRead(seq, next)
            guard t1 == 0x30 else { throw RsaError.invalidKey }
            _ = alg
            let (t2, octet, _) = try derRead(seq, next1)
            guard t2 == 0x04 else { throw RsaError.invalidKey }
            found = try parsePKCS1Private(octet)
        } catch {
            // 退化：整段就是 PKCS#1
            found = try parsePKCS1Private(seq)
        }
        guard let key = found else { throw RsaError.invalidKey }
        return key
    }

    private static func parsePKCS1Private(_ der: [UInt8]) throws -> ParsedKey {
        let (tag, seq, _) = try derRead(der, 0)
        guard tag == 0x30 else { throw RsaError.invalidKey }
        var pos = 0
        var ints = [BigUInt]()
        while pos < seq.count {
            let (t, c, next) = try derRead(seq, pos)
            guard t == 0x02 else { throw RsaError.invalidKey }
            ints.append(try derParseInteger(c))
            pos = next
        }
        guard ints.count >= 3 else { throw RsaError.invalidKey }
        // ints: version, n, e, d, ...
        return ParsedKey(modulus: ints[1], exponent: ints[3])
    }

    /// 解析 SPKI 公钥。
    static func parseSPKI(_ der: [UInt8]) throws -> ParsedKey {
        let (tag, seq, _) = try derRead(der, 0)
        guard tag == 0x30 else { throw RsaError.invalidKey }
        let (t1, alg, next) = try derRead(seq, 0)
        guard t1 == 0x30 else { throw RsaError.invalidKey }
        _ = alg
        let (t2, bit, _) = try derRead(seq, next)
        guard t2 == 0x03, bit.count >= 2, bit[0] == 0x00 else { throw RsaError.invalidKey }
        let (t3, pkcs1, _) = try derRead(Array(bit[1...]), 0)
        guard t3 == 0x30 else { throw RsaError.invalidKey }
        var pos = 0
        var ints = [BigUInt]()
        while pos < pkcs1.count {
            let (t, c, n2) = try derRead(pkcs1, pos)
            guard t == 0x02 else { throw RsaError.invalidKey }
            ints.append(try derParseInteger(c))
            pos = n2
        }
        guard ints.count >= 2 else { throw RsaError.invalidKey }
        return ParsedKey(modulus: ints[0], exponent: ints[1])
    }
}
