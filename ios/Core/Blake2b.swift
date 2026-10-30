import Foundation

/// 64 位循环右移（Swift 标准库无内建 rotate，SE-0199 未落地）。
func rotr64(_ x: UInt64, _ n: UInt32) -> UInt64 {
    n == 0 ? x : (x >> n) | (x << (64 - n))
}

/// Blake2b（RFC 7693），流式接口，摘要长度 1..64。
/// IV / SIGMA / G 轮函数与 vendor/argon2/src/blake2/blake2b.c 一致。
/// 移植基准：android/.../Crypto.kt 的 Blake2b（本会话已对齐黄金向量）。
struct Blake2b {
    private static let blockLen = 128
    private static let iv: [UInt64] = [
        0x6a09_e667_f3bc_c908, 0xbb67_ae85_84ca_a73b,
        0x3c6e_f372_fe94_f82b, 0xa54f_f53a_5f1d_36f1,
        0x510e_527f_ade6_82d1, 0x9b05_688c_2b3e_6c1f,
        0x1f83_d9ab_fb41_bd6b, 0x5be0_cd19_137e_2179
    ]
    private static let sigma: [[UInt8]] = [
        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
        [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
        [11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4],
        [7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8],
        [9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13],
        [2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9],
        [12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11],
        [13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10],
        [6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5],
        [10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0]
    ]

    private var h: [UInt64]
    private var buf = [UInt8](repeating: 0, count: blockLen)
    private var bufLen = 0
    private var t0: UInt64 = 0
    private var t1: UInt64 = 0
    private let outLen: Int

    init(outLen: Int) {
        precondition(outLen >= 1 && outLen <= 64, "blake2b outlen must be 1..64")
        self.outLen = outLen
        var hh = Self.iv
        // 参数块：0x01010000 | keylen(0) | outlen
        hh[0] ^= 0x0101_0000 ^ UInt64(outLen)
        h = hh
    }

    mutating func update(_ data: [UInt8]) {
        var off = 0
        var remaining = data.count
        while remaining > 0 {
            if bufLen == Self.blockLen { // 先把满缓冲刷出去
                t0 &+= UInt64(Self.blockLen)
                if t0 == 0 { t1 &+= 1 }
                compress(buf, last: false)
                bufLen = 0
            }
            let take = min(remaining, Self.blockLen - bufLen)
            buf.replaceSubrange(bufLen..<(bufLen + take), with: data[off..<(off + take)])
            bufLen += take
            off += take
            remaining -= take
        }
    }

    mutating func updateLe32(_ value: UInt32) {
        update([UInt8(value & 0xFF), UInt8((value >> 8) & 0xFF),
                UInt8((value >> 16) & 0xFF), UInt8((value >> 24) & 0xFF)])
    }

    mutating func final() -> [UInt8] {
        var out = [UInt8](repeating: 0, count: outLen)
        t0 &+= UInt64(bufLen)
        if t0 == 0 && bufLen > 0 { t1 &+= 1 }
        for i in bufLen..<Self.blockLen { buf[i] = 0 }
        compress(buf, last: true)
        for i in 0..<outLen {
            out[i] = UInt8((h[i / 8] >> (8 * (i % 8))) & 0xFF)
        }
        return out
    }

    private mutating func compress(_ block: [UInt8], last: Bool) {
        var m = [UInt64](repeating: 0, count: 16)
        for i in 0..<16 {
            var v: UInt64 = 0
            for j in (0...7).reversed() {
                v = (v << 8) | UInt64(block[8 * i + j])
            }
            m[i] = v
        }
        var v = h // RFC 7693：v[0..7] = 状态 h，v[8..15] = IV
        v.append(contentsOf: Self.iv)
        v[12] ^= t0
        v[13] ^= t1
        if last { v[14] = ~v[14] }

        for r in 0..<12 {
            let s = Self.sigma[r % 10]
            g(&v, 0, 4, 8, 12, m[Int(s[0])], m[Int(s[1])])
            g(&v, 1, 5, 9, 13, m[Int(s[2])], m[Int(s[3])])
            g(&v, 2, 6, 10, 14, m[Int(s[4])], m[Int(s[5])])
            g(&v, 3, 7, 11, 15, m[Int(s[6])], m[Int(s[7])])
            g(&v, 0, 5, 10, 15, m[Int(s[8])], m[Int(s[9])])
            g(&v, 1, 6, 11, 12, m[Int(s[10])], m[Int(s[11])])
            g(&v, 2, 7, 8, 13, m[Int(s[12])], m[Int(s[13])])
            g(&v, 3, 4, 9, 14, m[Int(s[14])], m[Int(s[15])])
        }
        for i in 0..<8 { h[i] ^= v[i] ^ v[i + 8] }
    }

    private func g(_ v: inout [UInt64], _ a: Int, _ b: Int, _ c: Int, _ d: Int,
                   _ x: UInt64, _ y: UInt64) {
        v[a] = v[a] &+ v[b] &+ x
        v[d] = rotr64(v[d] ^ v[a], 32)
        v[c] = v[c] &+ v[d]
        v[b] = rotr64(v[b] ^ v[c], 24)
        v[a] = v[a] &+ v[b] &+ y
        v[d] = rotr64(v[d] ^ v[a], 16)
        v[c] = v[c] &+ v[d]
        v[b] = rotr64(v[b] ^ v[c], 63)
    }
}

/// blake2b_long（blake2/blake2b.c）：可变长度哈希。
/// outLen<=64 时输出 Blake2b(outLen, LE32(outLen) || in)；否则按参考实现链式生成。
func blake2bLong(_ outLen: Int, _ input: [UInt8]) -> [UInt8] {
    let outLenBytes: [UInt8] = [
        UInt8(outLen & 0xFF), UInt8((outLen >> 8) & 0xFF),
        UInt8((outLen >> 16) & 0xFF), UInt8((outLen >> 24) & 0xFF)
    ]
    if outLen <= 64 {
        var b = Blake2b(outLen: outLen)
        b.update(outLenBytes)
        b.update(input)
        return b.final()
    }
    var outBuffer = Blake2b(outLen: 64)
    outBuffer.update(outLenBytes)
    outBuffer.update(input)
    var ob = outBuffer.final()

    var out = [UInt8](repeating: 0, count: outLen)
    out.replaceSubrange(0..<32, with: ob[0..<32])
    var produced = 32
    var toProduce = outLen - 32
    var inBuffer = [UInt8](repeating: 0, count: 64)
    while toProduce > 64 {
        for i in 0..<64 { inBuffer[i] = ob[i] }
        var nb = Blake2b(outLen: 64)
        nb.update(inBuffer)
        ob = nb.final()
        out.replaceSubrange(produced..<(produced + 32), with: ob[0..<32])
        produced += 32
        toProduce -= 32
    }
    for i in 0..<64 { inBuffer[i] = ob[i] }
    var nb = Blake2b(outLen: toProduce)
    nb.update(inBuffer)
    ob = nb.final()
    out.replaceSubrange(produced..<(produced + toProduce), with: ob[0..<toProduce])
    return out
}
