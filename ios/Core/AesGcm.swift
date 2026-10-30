import Foundation

/// AES-256-GCM（SP 800-38D）纯 Swift 实现。
/// 不引第三方依赖：五端（Windows/Linux/macOS/Android/iOS）各自用同一份
/// 黄金向量兜底，本文件保证 iOS/Linux/macOS(CI) 行为一致。
///
/// 布局约定与桌面端一致：nonce 12 字节、tag 16 字节、密文长度 == 明文长度。

// MARK: - AES 核心（仅解密方需要等价解密，GCM 统一走 CTR，故只需加密方向）

enum AES256 {
    private static let sbox: [UInt8] = [
        0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
        0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
        0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
        0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
        0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
        0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
        0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
        0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
        0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
        0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
        0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
        0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
        0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
        0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
        0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
        0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
    ]

    /// 轮密钥展开：AES-256 → 60 个 32 位字（15 轮）。
    static func keyExpansion(_ key: [UInt8]) -> [UInt32] {
        precondition(key.count == 32)
        var w = [UInt32](repeating: 0, count: 60)
        for i in 0..<8 {
            w[i] = (UInt32(key[4 * i]) << 24) | (UInt32(key[4 * i + 1]) << 16)
                | (UInt32(key[4 * i + 2]) << 8) | UInt32(key[4 * i + 3])
        }
        var rcon: UInt8 = 0x01
        for i in 8..<60 {
            var temp = w[i - 1]
            if i % 8 == 0 {
                temp = rotWord(subWord(temp)) ^ (UInt32(rcon) << 24)
                rcon = xtime(rcon)
            } else if i % 8 == 4 {
                temp = subWord(temp)
            }
            w[i] = w[i - 8] ^ temp
        }
        return w
    }

    static func encryptBlock(_ input: [UInt8], _ w: [UInt32]) -> [UInt8] {
        // 状态为列主序：字节 i 的行 = i % 4，列 = i / 4（与 FIPS 197 一致）
        var s = [UInt8](repeating: 0, count: 16)
        for i in 0..<16 {
            s[i] = input[i] ^ UInt8((w[i / 4] >> (24 - 8 * (i % 4))) & 0xFF)
        }
        var t = [UInt8](repeating: 0, count: 16)
        for round in 1...14 {
            // SubBytes + ShiftRows（行 r 循环左移 r：源列 c 移到列 (c-r)%4）
            for i in 0..<16 {
                let r = i % 4
                let c = i / 4
                t[4 * ((c + 4 - r) % 4) + r] = sbox[Int(s[i])]
            }
            // MixColumns（末轮省略）
            if round < 14 {
                for c in 0..<4 {
                    let a0 = t[4 * c], a1 = t[4 * c + 1], a2 = t[4 * c + 2], a3 = t[4 * c + 3]
                    s[4 * c] = xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3
                    s[4 * c + 1] = a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3
                    s[4 * c + 2] = a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3)
                    s[4 * c + 3] = (xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3)
                }
            } else {
                s = t
            }
            // AddRoundKey
            for i in 0..<16 {
                s[i] ^= UInt8((w[4 * round + i / 4] >> (24 - 8 * (i % 4))) & 0xFF)
            }
        }
        return s
    }

    private static func subWord(_ x: UInt32) -> UInt32 {
        (UInt32(sbox[Int((x >> 24) & 0xFF)]) << 24) | (UInt32(sbox[Int((x >> 16) & 0xFF)]) << 16)
            | (UInt32(sbox[Int((x >> 8) & 0xFF)]) << 8) | UInt32(sbox[Int(x & 0xFF)])
    }

    private static func rotWord(_ x: UInt32) -> UInt32 {
        (x << 8) | (x >> 24)
    }

    /// GF(2^8) 乘 2；rcon 用乘 2 直到回到 1 会经过 0x80→0x1b，xtime 已含约简。
    private static func xtime(_ x: UInt8) -> UInt8 {
        let doubled = UInt16(x) << 1
        return UInt8((doubled ^ (doubled >= 0x100 ? 0x11b : 0)) & 0xFF)
    }
}

// MARK: - GCM

enum GCMError: Error, Equatable {
    case authenticationFailed // 认证失败（含密文篡改）：对外统一映射「密码错误」
    case invalidInput
}

enum AESGCM {
    static let nonceLen = 12
    static let tagLen = 16

    /// 返回 (密文, tag)；与桌面端容器布局一致，密文与 tag 分开保存。
    static func encrypt(key: [UInt8], nonce: [UInt8], aad: [UInt8],
                        plaintext: [UInt8]) throws -> ([UInt8], [UInt8]) {
        guard key.count == 32, nonce.count == nonceLen else { throw GCMError.invalidInput }
        let w = AES256.keyExpansion(key)
        let h = aesEnc(zeroBlock(), w)
        let j0 = j0FromNonce(nonce)

        var cipher = [UInt8](repeating: 0, count: plaintext.count)
        ctrCrypt(w, j0, plaintext, &cipher)

        var s = ghash(h, aad, cipher)
        let lenBlock = lengthBlock(aad.count, cipher.count)
        for i in 0..<16 { s[i] ^= lenBlock[i] }
        gmul128(&s, h) // lenBlock 作为最后一个块被吸收：先异或再乘（NIST 附 A 步骤 5）
        let ekJ0 = aesEnc(j0, w)
        var tag = [UInt8](repeating: 0, count: tagLen)
        for i in 0..<tagLen { tag[i] = s[i] ^ ekJ0[i] }
        return (cipher, tag)
    }

    /// 认证失败抛 GCMError.authenticationFailed；调用方按需求统一映射文案。
    static func decrypt(key: [UInt8], nonce: [UInt8], aad: [UInt8],
                        ciphertext: [UInt8], tag: [UInt8]) throws -> [UInt8] {
        guard key.count == 32, nonce.count == nonceLen, tag.count == tagLen else {
            throw GCMError.invalidInput
        }
        let w = AES256.keyExpansion(key)
        let h = aesEnc(zeroBlock(), w)
        let j0 = j0FromNonce(nonce)

        var s = ghash(h, aad, ciphertext)
        let lenBlock = lengthBlock(aad.count, ciphertext.count)
        for i in 0..<16 { s[i] ^= lenBlock[i] }
        gmul128(&s, h) // 同上：lenBlock 吸收后必须再做一次域乘
        let ekJ0 = aesEnc(j0, w)
        var expected = [UInt8](repeating: 0, count: tagLen)
        for i in 0..<tagLen { expected[i] = s[i] ^ ekJ0[i] }
        // 恒定时间比较
        var diff: UInt8 = 0
        for i in 0..<tagLen { diff |= expected[i] ^ tag[i] }
        if diff != 0 { throw GCMError.authenticationFailed }

        var plain = [UInt8](repeating: 0, count: ciphertext.count)
        ctrCrypt(w, j0, ciphertext, &plain)
        return plain
    }

    // ---- 内部 ----

    private static func zeroBlock() -> [UInt8] { [UInt8](repeating: 0, count: 16) }

    private static func aesEnc(_ block: [UInt8], _ w: [UInt32]) -> [UInt8] {
        AES256.encryptBlock(block, w)
    }

    private static func j0FromNonce(_ nonce: [UInt8]) -> [UInt8] {
        // 12 字节 nonce：J0 = nonce || 0x00000001（SP 800-38D §8.1.1）
        var j0 = nonce
        j0.append(contentsOf: [0, 0, 0, 1])
        return j0
    }

    private static func inc32(_ block: inout [UInt8]) {
        var ctr: UInt32 = 0
        for i in 12..<16 { ctr = (ctr << 8) | UInt32(block[i]) }
        ctr &+= 1
        for i in (12..<16).reversed() {
            block[i] = UInt8(ctr & 0xFF)
            ctr >>= 8
        }
    }

    private static func ctrCrypt(_ w: [UInt32], _ j0: [UInt8],
                                 _ input: [UInt8], _ output: inout [UInt8]) {
        // GCM 计数器：密文流从 inc32(J0) 起步（J0 本身留给 tag 掩码）。
        // 这一步之前误用 E(K,J0) 作首块，是密文/_tag 全错的根因。
        var cb = j0
        var off = 0
        while off < input.count {
            inc32(&cb)
            let ks = aesEnc(cb, w)
            let take = min(16, input.count - off)
            for i in 0..<take { output[off + i] = input[off + i] ^ ks[i] }
            off += take
        }
    }

    /// GHASH：H 授权后按 16 字节分块吸收 AAD 与密文（零填充）。
    private static func ghash(_ h: [UInt8], _ aad: [UInt8], _ cipher: [UInt8]) -> [UInt8] {
        var y = [UInt8](repeating: 0, count: 16)
        func absorb(_ data: [UInt8]) {
            var off = 0
            while off < data.count {
                let take = min(16, data.count - off)
                for i in 0..<take { y[i] ^= data[off + i] }
                gmul128(&y, h)
                off += take
            }
        }
        absorb(aad)
        absorb(cipher)
        return y
    }

    private static func lengthBlock(_ aadLen: Int, _ ctLen: Int) -> [UInt8] {
        var b = [UInt8](repeating: 0, count: 16)
        let aBits = UInt64(aadLen) &* 8
        let cBits = UInt64(ctLen) &* 8
        for i in 0..<8 { b[i] = UInt8((aBits >> (8 * (7 - i))) & 0xFF) }
        for i in 0..<8 { b[8 + i] = UInt8((cBits >> (8 * (7 - i))) & 0xFF) }
        return b
    }

    /// GF(2^128) 乘法（SP 800-38D 附 A：MSB-first 位序，R = 0xE1 || 0^120）。
    private static func gmul128(_ x: inout [UInt8], _ h: [UInt8]) {
        var z = [UInt8](repeating: 0, count: 16)
        var v = h
        for i in 0..<128 {
            let bit = (x[i / 8] >> (7 - (i % 8))) & 1
            if bit == 1 {
                for k in 0..<16 { z[k] ^= v[k] }
            }
            // V 整体右移一位（MSB-first：高字节 b[k-1] 的 bit0 移入 b[k] 的
            // bit7，b[15] 的 bit0 移出；移出为 1 则异或 R = 0xE1||0^120 进 v[0]）
            let lsb = v[15] & 1
            for k in stride(from: 15, through: 1, by: -1) {
                v[k] = (v[k] >> 1) | ((v[k - 1] & 1) << 7)
            }
            v[0] >>= 1
            if lsb == 1 { v[0] ^= 0xE1 }
        }
        x = z
    }
}
