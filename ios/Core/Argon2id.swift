import Foundation

/// Argon2id 纯 Swift 移植（RFC 9106），函数与 vendor/argon2/src/ref.c、
/// core.c 一一对应，命名保留 C 侧原名便于对照审阅。
///
/// 移植基准：android/.../Crypto.kt 的 Argon2id（本会话已对齐黄金向量，
/// 含 roundRow G6/G8 第三参 81/65 不可互换的关键坑位）。
enum Argon2id {
    static let typeArgon2id: UInt32 = 2
    static let syncPoints = 4
    static let addressesInBlock = 128
    static let blockBytes = 1024
    static let prehashDigestLen = 64
    static let prehashSeedLen = 72

    static func derive(
        password: [UInt8],
        salt: [UInt8],
        memoryKiB: Int,
        iterations: Int,
        lanes: Int,
        outputLen: Int
    ) -> [UInt8] {
        var memoryBlocks = memoryKiB
        if memoryBlocks < 2 * syncPoints * lanes {
            memoryBlocks = 2 * syncPoints * lanes
        }
        let segmentLength = memoryBlocks / (lanes * syncPoints)
        memoryBlocks = segmentLength * (lanes * syncPoints) // 保证各段等长
        let laneLength = segmentLength * syncPoints

        // initial_hash（core.c）：H0 = Blake2b-512(全部输入参数)
        var hb = Blake2b(outLen: prehashDigestLen)
        hb.updateLe32(UInt32(lanes))
        hb.updateLe32(UInt32(outputLen))
        hb.updateLe32(UInt32(memoryKiB))
        hb.updateLe32(UInt32(iterations))
        hb.updateLe32(UInt32(Crypto.argon2Version))
        hb.updateLe32(typeArgon2id)
        hb.updateLe32(UInt32(password.count))
        hb.update(password)
        hb.updateLe32(UInt32(salt.count))
        hb.update(salt)
        hb.updateLe32(0) // secretlen（本工程不使用）
        hb.updateLe32(0) // adlen
        let h0 = hb.final()

        // memory[lane][index] = 128 × u64
        var memory = [[UInt64]](
            repeating: [UInt64](repeating: 0, count: 128), count: laneLength * lanes
        )

        // fill_first_blocks：B[l][0..1] = H'(H0 || LE32(j) || LE32(l))
        for lane in 0..<lanes {
            for j in 0...1 {
                // C 的 blockhash 缓冲 72 字节：64 字节哈希 + 8 字节零扩展
                //（Kotlin copyOf 超长补零同义）；Swift 切片不补零，须显式扩。
                var seed = h0 + [UInt8](repeating: 0,
                                        count: prehashSeedLen - prehashDigestLen)
                store32(&seed, prehashDigestLen, UInt32(j))
                store32(&seed, prehashDigestLen + 4, UInt32(lane))
                let blockBytes = blake2bLong(Self.blockBytes, seed)
                loadBlock(&memory[lane * laneLength + j], blockBytes)
            }
        }

        // fill_memory_blocks（串行，与 ref.c 单线程路径一致）
        for pass in 0..<iterations {
            for slice in 0..<syncPoints {
                for lane in 0..<lanes {
                    fillSegment(&memory, lanes: lanes, passes: iterations, pass: pass,
                                slice: slice, lane: lane, segmentLength: segmentLength,
                                laneLength: laneLength, memoryBlocks: memoryBlocks)
                }
            }
        }

        // finalize：各 lane 末块 XOR 后 blake2bLong(outLen, ...)
        var blockhash = memory[laneLength - 1]
        if lanes > 1 {
            for lane in 1..<lanes {
                let last = memory[lane * laneLength + laneLength - 1]
                for i in 0..<128 { blockhash[i] ^= last[i] }
            }
        }
        var blockBytesArr = [UInt8](repeating: 0, count: blockBytes)
        storeBlock(&blockBytesArr, blockhash)
        return blake2bLong(outputLen, blockBytesArr)
    }

    private static func fillSegment(
        _ memory: inout [[UInt64]],
        lanes: Int, passes: Int, pass: Int, slice: Int, lane: Int,
        segmentLength: Int, laneLength: Int, memoryBlocks: Int
    ) {
        // ref.c：Argon2id 只在第一遍的前两个分片用数据独立寻址
        let dataIndependentAddressing = pass == 0 && slice < syncPoints / 2
        var address = [UInt64](repeating: 0, count: 128)
        var input = [UInt64](repeating: 0, count: 128)
        let zero = [UInt64](repeating: 0, count: 128)
        if dataIndependentAddressing {
            input[0] = UInt64(pass)
            input[1] = UInt64(lane)
            input[2] = UInt64(slice)
            input[3] = UInt64(memoryBlocks)
            input[4] = UInt64(passes)
            input[5] = UInt64(typeArgon2id)
        }

        var startingIndex = 0
        if pass == 0 && slice == 0 {
            startingIndex = 2 // 前两块已由 H' 生成
            if dataIndependentAddressing { nextAddresses(&address, &input, zero) }
        }

        var currOffset = lane * laneLength + slice * segmentLength + startingIndex
        var prevOffset = currOffset % laneLength == 0
            ? currOffset + laneLength - 1
            : currOffset - 1

        var i = startingIndex
        while i < segmentLength {
            if currOffset % laneLength == 1 { prevOffset = currOffset - 1 }

            let pseudoRand: UInt64
            if dataIndependentAddressing {
                if i % addressesInBlock == 0 { nextAddresses(&address, &input, zero) }
                pseudoRand = address[i % addressesInBlock]
            } else {
                pseudoRand = memory[prevOffset][0]
            }

            var refLane = Int((pseudoRand >> 32) % UInt64(lanes))
            if pass == 0 && slice == 0 { refLane = lane } // 第一遍首分片不能跨 lane

            let refIndex = indexAlpha(pass: pass, slice: slice, index: i,
                                      laneLength: laneLength, segmentLength: segmentLength,
                                      pseudoRand: pseudoRand & 0xFFFF_FFFF,
                                      sameLane: refLane == lane)

            let refLaneBase = refLane * laneLength
            let prev = Array(memory[prevOffset])
            let ref = Array(memory[refLaneBase + refIndex])
            var next = memory[currOffset]
            fillBlock(prev, ref, &next, withXor: pass != 0)
            memory[currOffset] = next
            i += 1
            currOffset += 1
            prevOffset += 1
        }
    }

    // core.c index_alpha：伪随机值平方映射到引用区
    private static func indexAlpha(
        pass: Int, slice: Int, index: Int, laneLength: Int, segmentLength: Int,
        pseudoRand: UInt64, sameLane: Bool
    ) -> Int {
        let referenceAreaSize: Int
        if pass == 0 {
            if slice == 0 {
                referenceAreaSize = index - 1
            } else if sameLane {
                referenceAreaSize = slice * segmentLength + index - 1
            } else {
                referenceAreaSize = slice * segmentLength + (index == 0 ? -1 : 0)
            }
        } else if sameLane {
            referenceAreaSize = laneLength - segmentLength + index - 1
        } else {
            referenceAreaSize = laneLength - segmentLength + (index == 0 ? -1 : 0)
        }

        var relative = pseudoRand
        relative = (relative &* relative) >> 32
        let mapped = (UInt64(referenceAreaSize) &* relative) >> 32
        let relativePosition = referenceAreaSize - 1 - Int(mapped)

        let startPosition = pass != 0
            ? (slice == syncPoints - 1 ? 0 : (slice + 1) * segmentLength)
            : 0
        return (startPosition + relativePosition) % laneLength
    }

    // ref.c next_addresses：G(zero, input) → G(zero, address) 两步推进地址块
    private static func nextAddresses(
        _ address: inout [UInt64], _ input: inout [UInt64], _ zero: [UInt64]
    ) {
        input[6] &+= 1
        var a = address
        fillBlock(zero, input, &a, withXor: false)
        fillBlock(zero, a, &a, withXor: false)
        address = a
    }

    // ref.c fill_block：R = ref XOR prev；列轮 + 行轮 blamka；next = tmp XOR R
    private static func fillBlock(
        _ prev: [UInt64], _ ref: [UInt64], _ next: inout [UInt64], withXor: Bool
    ) {
        var blockR = [UInt64](repeating: 0, count: 128)
        var blockTmp = [UInt64](repeating: 0, count: 128)
        for i in 0..<128 { blockR[i] = ref[i] ^ prev[i] }
        for i in 0..<128 { blockTmp[i] = blockR[i] }
        if withXor {
            for i in 0..<128 { blockTmp[i] ^= next[i] }
        }
        for i in 0..<8 { roundColumn(&blockR, 16 * i) }
        for i in 0..<8 { roundRow(&blockR, i) }
        for i in 0..<128 { next[i] = blockTmp[i] ^ blockR[i] }
    }

    // blamka-round-ref.h：fBlaMka + G + BLAKE2_ROUND_NOMSG
    private static func fBlaMka(_ x: UInt64, _ y: UInt64) -> UInt64 {
        let mask: UInt64 = 0xFFFF_FFFF
        let xy = (x & mask) &* (y & mask) // 与 C 的无符号乘法同样按 2^64 截断
        return x &+ y &+ 2 &* xy
    }

    private static func gRound(_ v: inout [UInt64], _ a: Int, _ b: Int, _ c: Int, _ d: Int) {
        v[a] = fBlaMka(v[a], v[b])
        v[d] = rotr64(v[d] ^ v[a], 32)
        v[c] = fBlaMka(v[c], v[d])
        v[b] = rotr64(v[b] ^ v[c], 24)
        v[a] = fBlaMka(v[a], v[b])
        v[d] = rotr64(v[d] ^ v[a], 16)
        v[c] = fBlaMka(v[c], v[d])
        v[b] = rotr64(v[b] ^ v[c], 63)
    }

    private static func roundColumn(_ v: inout [UInt64], _ o: Int) {
        gRound(&v, o, o + 4, o + 8, o + 12)
        gRound(&v, o + 1, o + 5, o + 9, o + 13)
        gRound(&v, o + 2, o + 6, o + 10, o + 14)
        gRound(&v, o + 3, o + 7, o + 11, o + 15)
        gRound(&v, o, o + 5, o + 10, o + 15)
        gRound(&v, o + 1, o + 6, o + 11, o + 12)
        gRound(&v, o + 2, o + 7, o + 8, o + 13)
        gRound(&v, o + 3, o + 4, o + 9, o + 14)
    }

    private static func roundRow(_ v: inout [UInt64], _ o: Int) {
        // BLAKE2_ROUND_NOMSG 的 G 序列作用在行排列 rowperm = [x, x+1, x+16,
        // x+17, x+32, x+33, ..., x+112, x+113]（x = 2*o）上：
        // G(rowperm[k], rowperm[k+4], rowperm[k+8], rowperm[k+12])。
        // 关键映射：G6 = rowperm[1,6,11,12] = (x+1, x+48, x+81, x+96)；
        //          G8 = rowperm[3,4,9,14]  = (x+17, x+32, x+65, x+112)。
        // G6 的第三参与 G8 的第三参极易写反（65/81），须以预处理展开为准。
        let x = 2 * o
        gRound(&v, x, x + 32, x + 64, x + 96)
        gRound(&v, x + 1, x + 33, x + 65, x + 97)
        gRound(&v, x + 16, x + 48, x + 80, x + 112)
        gRound(&v, x + 17, x + 49, x + 81, x + 113)
        gRound(&v, x, x + 33, x + 80, x + 113)
        gRound(&v, x + 1, x + 48, x + 81, x + 96)
        gRound(&v, x + 16, x + 49, x + 64, x + 97)
        gRound(&v, x + 17, x + 32, x + 65, x + 112)
    }

    private static func store32(_ dst: inout [UInt8], _ off: Int, _ value: UInt32) {
        dst[off] = UInt8(value & 0xFF)
        dst[off + 1] = UInt8((value >> 8) & 0xFF)
        dst[off + 2] = UInt8((value >> 16) & 0xFF)
        dst[off + 3] = UInt8((value >> 24) & 0xFF)
    }

    private static func loadBlock(_ block: inout [UInt64], _ bytes: [UInt8]) {
        for i in 0..<128 {
            var v: UInt64 = 0
            for j in (0...7).reversed() {
                v = (v << 8) | UInt64(bytes[8 * i + j])
            }
            block[i] = v
        }
    }

    private static func storeBlock(_ bytes: inout [UInt8], _ block: [UInt64]) {
        for i in 0..<128 {
            var v = block[i]
            for j in 0..<8 {
                bytes[8 * i + j] = UInt8(v & 0xFF)
                v >>= 8
            }
        }
    }
}
