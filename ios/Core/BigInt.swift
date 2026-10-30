import Foundation

/// 32 位 limbs 的大整数（无符号），只实现 RSA-2048 所需的运算子集。
/// 设计取舍：数量级只有 64-66 limbs，直接用 schoolbook 乘法与 Knuth 除法（算法 D），
/// 不做 Montgomery/Barrett——正确性优先，性能实测可接受（modexp 约 100ms 级）。
struct BigUInt {
    /// 小端 limbs（limb[i] 是 2^(32i) 位的值）；规约：无前导零（值为 0 时 limbs == [0]）
    var limbs: [UInt32]

    static let zero = BigUInt(limbs: [0])

    var isZero: Bool { limbs == [0] }
    var bitCount: Int {
        for i in stride(from: limbs.count - 1, through: 0, by: -1) {
            let v = limbs[i]
            if v != 0 { return i * 32 + (32 - v.leadingZeroBitCount) }
        }
        return 0
    }

    /// bit(n)：n 从 0（最低位）计
    func bit(_ n: Int) -> UInt32 {
        let limb = n / 32
        if limb >= limbs.count { return 0 }
        return (limbs[limb] >> UInt32(n % 32)) & 1
    }

    static func fromBytesBE(_ bytes: [UInt8]) -> BigUInt {
        var limbs = [UInt32]()
        limbs.reserveCapacity((bytes.count + 3) / 4)
        var i = bytes.count
        while i > 0 {
            var limb: UInt32 = 0
            let lo = max(0, i - 4)
            for j in lo..<i {
                limb = (limb << 8) | UInt32(bytes[j])
            }
            limbs.append(limb)
            i = lo
        }
        var r = BigUInt(limbs: limbs)
        r.normalize()
        return r
    }

    /// 定长 BE 字节序输出（RSA 用：不足补前导零，过长截断高位）。
    /// out[j] 承载数值的位 [8*(width-1-j), +8)（位 0 = LSB）。
    func bytesBE(_ width: Int) -> [UInt8] {
        var out = [UInt8](repeating: 0, count: width)
        for j in 0..<width {
            let lowBit = (width - 1 - j) * 8
            let limb = lowBit / 32
            let shift = lowBit % 32
            var v: UInt32 = 0
            if limb < limbs.count {
                v = limbs[limb] >> UInt32(shift)
                if shift > 0 && limb + 1 < limbs.count {
                    v |= limbs[limb + 1] << UInt32(32 - shift)
                }
            }
            out[j] = UInt8(v & 0xFF)
        }
        return out
    }

    mutating func normalize() {
        while limbs.count > 1 && limbs.last == 0 { limbs.removeLast() }
    }

    // ---- 比较 ----

    static func compare(_ a: BigUInt, _ b: BigUInt) -> Int {
        let n = max(a.limbs.count, b.limbs.count)
        for i in stride(from: n - 1, through: 0, by: -1) {
            let av = i < a.limbs.count ? a.limbs[i] : 0
            let bv = i < b.limbs.count ? b.limbs[i] : 0
            if av < bv { return -1 }
            if av > bv { return 1 }
        }
        return 0
    }

    static func < (a: BigUInt, b: BigUInt) -> Bool { compare(a, b) < 0 }
    static func <= (a: BigUInt, b: BigUInt) -> Bool { compare(a, b) <= 0 }
    static func >= (a: BigUInt, b: BigUInt) -> Bool { compare(a, b) >= 0 }

    // ---- 加减（供除法与 keygen 使用）----

    static func add(_ a: BigUInt, _ b: BigUInt) -> BigUInt {
        var out = [UInt32](repeating: 0, count: max(a.limbs.count, b.limbs.count) + 1)
        var carry: UInt64 = 0
        for i in 0..<out.count {
            let av: UInt64 = i < a.limbs.count ? UInt64(a.limbs[i]) : 0
            let bv: UInt64 = i < b.limbs.count ? UInt64(b.limbs[i]) : 0
            let s = av + bv + carry
            out[i] = UInt32(truncatingIfNeeded: s)
            carry = s >> 32
        }
        var r = BigUInt(limbs: out)
        r.normalize()
        return r
    }

    /// 要求 a >= b
    static func sub(_ a: BigUInt, _ b: BigUInt) -> BigUInt {
        var out = [UInt32](repeating: 0, count: a.limbs.count)
        var borrow: Int64 = 0
        for i in 0..<a.limbs.count {
            let av = Int64(a.limbs[i])
            let bv = i < b.limbs.count ? Int64(b.limbs[i]) : 0
            var d = av - bv - borrow
            if d < 0 { d += 0x1_0000_0000; borrow = 1 } else { borrow = 0 }
            out[i] = UInt32(truncatingIfNeeded: d)
        }
        var r = BigUInt(limbs: out)
        r.normalize()
        return r
    }

    // ---- 乘法 ----

    static func mul(_ a: BigUInt, _ b: BigUInt) -> BigUInt {
        if a.isZero || b.isZero { return .zero }
        var out = [UInt32](repeating: 0, count: a.limbs.count + b.limbs.count)
        for i in 0..<a.limbs.count {
            var carry: UInt64 = 0
            let av = UInt64(a.limbs[i])
            if av == 0 { continue }
            for j in 0..<b.limbs.count {
                let cur = UInt64(out[i + j]) + av * UInt64(b.limbs[j]) + carry
                out[i + j] = UInt32(truncatingIfNeeded: cur)
                carry = cur >> 32
            }
            var k = i + b.limbs.count
            while carry > 0 {
                let cur = UInt64(out[k]) + carry
                out[k] = UInt32(truncatingIfNeeded: cur)
                carry = cur >> 32
                k += 1
            }
        }
        var r = BigUInt(limbs: out)
        r.normalize()
        return r
    }

    // ---- 除法：Knuth 算法 D（余数返回）----

    static func mod(_ a: BigUInt, _ m: BigUInt) -> BigUInt {
        precondition(!m.isZero)
        if compare(a, m) < 0 { return a }
        if m.limbs.count == 1 {
            // 单 limb 快路径
            let d = UInt64(m.limbs[0])
            var rem: UInt64 = 0
            var out = a.limbs
            for i in stride(from: a.limbs.count - 1, through: 0, by: -1) {
                let cur = (rem << 32) | UInt64(a.limbs[i])
                out[i] = UInt32(truncatingIfNeeded: cur / d)
                rem = cur % d
            }
            var r = BigUInt(limbs: [UInt32(truncatingIfNeeded: rem)])
            r.normalize()
            return r
        }
        // 算法 D：以 32 位 limb 为基（b = 2^32）
        let n = m.limbs.count
        let shift = m.limbs[n - 1].leadingZeroBitCount
        let vNorm = shl(m, shift)          // 规范化除数
        let u = shl(a, shift)              // 规范化被除数（多一位空间）
        var ulimbs = u.limbs
        while ulimbs.count < a.limbs.count + n + 1 { ulimbs.append(0) }
        var q = [UInt32](repeating: 0, count: ulimbs.count - n)

        let vh = UInt64(vNorm.limbs[n - 1])
        let vl = UInt64(vNorm.limbs[n - 2])

        for j in stride(from: ulimbs.count - n - 1, through: 0, by: -1) {
            let numerator = (UInt64(ulimbs[j + n]) << 32) | UInt64(ulimbs[j + n - 1])
            var qhat = numerator / vh
            var rhat = numerator % vh
            while qhat >= 0x1_0000_0000 || qhat * vl > ((rhat << 32) | UInt64(ulimbs[j + n - 2])) {
                qhat -= 1
                rhat += vh
                if rhat >= 0x1_0000_0000 { break }
            }
            // 乘减
            var borrow: Int64 = 0
            var carry: UInt64 = 0
            for i in 0..<n {
                let p = qhat * UInt64(vNorm.limbs[i]) + carry
                carry = p >> 32
                let t = Int64(ulimbs[i + j]) - Int64(p & 0xFFFF_FFFF) - borrow
                if t < 0 { ulimbs[i + j] = UInt32(truncatingIfNeeded: t + 0x1_0000_0000); borrow = 1 }
                else { ulimbs[i + j] = UInt32(truncatingIfNeeded: t); borrow = 0 }
            }
            let t = Int64(ulimbs[j + n]) - Int64(carry) - borrow
            if t < 0 {
                // 加回（qhat 猜大 1，概率 ~2/b）
                ulimbs[j + n] = UInt32(truncatingIfNeeded: t + 0x1_0000_0000)
                qhat -= 1
                var c: UInt64 = 0
                for i in 0..<n {
                    let s = UInt64(ulimbs[i + j]) + UInt64(vNorm.limbs[i]) + c
                    ulimbs[i + j] = UInt32(truncatingIfNeeded: s)
                    c = s >> 32
                }
                ulimbs[j + n] = UInt32(truncatingIfNeeded: UInt64(ulimbs[j + n]) &+ c)
            } else {
                ulimbs[j + n] = UInt32(truncatingIfNeeded: t)
            }
            q[j] = UInt32(truncatingIfNeeded: qhat)
        }
        // 余数 = ulimbs[0..<n] 反规范化（右移补回规范化损失）
        let remLimbs = Array(ulimbs[0..<n])
        var r = BigUInt(limbs: remLimbs)
        r = shr(r, shift)
        r.normalize()
        _ = q
        return r
    }

    private static func shl(_ a: BigUInt, _ bits: Int) -> BigUInt {
        if bits == 0 { return a }
        let limbShift = bits / 32
        let bitShift = bits % 32
        var out = [UInt32](repeating: 0, count: a.limbs.count + limbShift + 1)
        for i in 0..<a.limbs.count {
            var v = UInt64(a.limbs[i]) << UInt32(bitShift)
            let idx = i + limbShift
            out[idx] |= UInt32(truncatingIfNeeded: v)
            v >>= 32
            out[idx + 1] |= UInt32(truncatingIfNeeded: v)
        }
        var r = BigUInt(limbs: out)
        r.normalize()
        return r
    }

    private static func shr(_ a: BigUInt, _ bits: Int) -> BigUInt {
        if bits == 0 { return a }
        let limbShift = bits / 32
        let bitShift = bits % 32
        if limbShift >= a.limbs.count { return .zero }
        var out = [UInt32](repeating: 0, count: a.limbs.count - limbShift)
        for i in 0..<out.count {
            var v = UInt64(a.limbs[i + limbShift]) >> UInt32(bitShift)
            if bitShift > 0 && i + limbShift + 1 < a.limbs.count {
                v |= UInt64(a.limbs[i + limbShift + 1]) << UInt32(32 - bitShift)
            }
            out[i] = UInt32(truncatingIfNeeded: v)
        }
        var r = BigUInt(limbs: out)
        r.normalize()
        return r
    }

    // ---- 模幂：从高位平方-乘 ----

    static func modPow(_ base: BigUInt, _ exp: BigUInt, _ m: BigUInt) -> BigUInt {
        if m.limbs.count == 1 {
            // 单 limb：用 UInt64 快路径
            let d = UInt64(m.limbs[0])
            var result: UInt64 = 1
            var b = UInt64(base.limbs[0]) % d
            let bits = exp.bitCount
            for i in stride(from: bits - 1, through: 0, by: -1) {
                result = (result * result) % d
                if exp.bit(i) == 1 { result = (result * b) % d }
            }
            var r = BigUInt(limbs: [UInt32(truncatingIfNeeded: result)])
            r.normalize()
            return r
        }
        var result = BigUInt(limbs: [1])
        result = mod(result, m)
        let b = mod(base, m)
        let bits = exp.bitCount
        for i in stride(from: bits - 1, through: 0, by: -1) {
            result = mod(mul(result, result), m)
            if exp.bit(i) == 1 {
                result = mod(mul(result, b), m)
            }
        }
        return result
    }

    // ---- gcd / 模逆（扩展欧几里得，供 keygen 用）----

    static func gcd(_ a: BigUInt, _ b: BigUInt) -> BigUInt {
        var x = a, y = b
        while !y.isZero {
            let r = mod(x, y)
            x = y
            y = r
        }
        return x
    }

    /// a * x ≡ 1 (mod m)；要求 gcd(a, m) == 1。
    /// 扩展欧几里得的 Bezout 系数会变号，用带符号幅值包装承载。
    static func modInverse(_ a: BigUInt, _ m: BigUInt) -> BigUInt {
        var oldR = a, r = m
        var oldS = SignedBig(neg: false, mag: BigUInt(limbs: [1]))
        var s = SignedBig.zero
        while !r.isZero {
            let (q, rem) = divMod(oldR, r)
            let tmp = SignedBig.sub(oldS, SignedBig.mulBig(s, q))
            oldR = r
            r = rem
            oldS = s
            s = tmp
        }
        // oldS 为带符号的 a^-1；规约到 [0, m)
        let (_, rem) = divMod(oldS.mag, m)
        return oldS.neg ? BigUInt.sub(m, rem) : rem
    }
}

/// 带符号幅值整数：仅扩展欧几里得使用（加减 + 乘无符号）。
struct SignedBig {
    var neg: Bool
    var mag: BigUInt

    static var zero: SignedBig { SignedBig(neg: false, mag: .zero) }

    static func sub(_ a: SignedBig, _ b: SignedBig) -> SignedBig {
        if a.neg == b.neg {
            let c = BigUInt.compare(a.mag, b.mag)
            if c >= 0 { return SignedBig(neg: a.neg, mag: BigUInt.sub(a.mag, b.mag)) }
            return SignedBig(neg: !a.neg, mag: BigUInt.sub(b.mag, a.mag))
        }
        return SignedBig(neg: a.neg, mag: BigUInt.add(a.mag, b.mag))
    }

    static func mulBig(_ a: SignedBig, _ b: BigUInt) -> SignedBig {
        SignedBig(neg: a.neg && !b.isZero, mag: BigUInt.mul(a.mag, b))
    }
}

extension BigUInt {
    /// 带商除法（供 modInverse 使用）
    static func divMod(_ a: BigUInt, _ d: BigUInt) -> (BigUInt, BigUInt) {
        precondition(!d.isZero)
        if compare(a, d) < 0 { return (.zero, a) }
        if d.limbs.count == 1 {
            let dd = UInt64(d.limbs[0])
            var rem: UInt64 = 0
            var q = a.limbs
            for i in stride(from: a.limbs.count - 1, through: 0, by: -1) {
                let cur = (rem << 32) | UInt64(a.limbs[i])
                q[i] = UInt32(truncatingIfNeeded: cur / dd)
                rem = cur % dd
            }
            var qq = BigUInt(limbs: q)
            qq.normalize()
            var rr = BigUInt(limbs: [UInt32(truncatingIfNeeded: rem)])
            rr.normalize()
            return (qq, rr)
        }
        // 通用：二分长除（位级）—— 位数 ~2048，每次迭代 O(limbs)，可接受
        var quotient = [UInt32](repeating: 0, count: a.limbs.count)
        var rem = BigUInt.zero
        for i in stride(from: a.bitCount - 1, through: 0, by: -1) {
            rem = shl(rem, 1)
            if a.bit(i) == 1 {
                rem.limbs[0] |= 1
            }
            if compare(rem, d) >= 0 {
                rem = sub(rem, d)
                quotient[i / 32] |= (1 << UInt32(i % 32))
            }
        }
        var qq = BigUInt(limbs: quotient)
        qq.normalize()
        rem.normalize()
        return (qq, rem)
    }
}
