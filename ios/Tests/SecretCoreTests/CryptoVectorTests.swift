import XCTest
@testable import SecretCore

/// 密码学原语黄金向量测试：与桌面三端、Android 跑同一份 test-vectors/。
final class CryptoVectorTests: XCTestCase {
    func testArgon2idVectorsMatchDesktop() throws {
        let list = try Vectors.vectors("kdf-argon2id.json")
        XCTAssertFalse(list.isEmpty, "no argon2id vectors")
        for v in list {
            let kdf = v["kdf"] as! [String: Any]
            let derived = Crypto.deriveKek(
                password: Vectors.bytes(kdf, "passwordHex"),
                salt: Vectors.bytes(kdf, "saltHex"),
                memoryKiB: kdf["memoryKiB"] as! Int,
                iterations: kdf["iterations"] as! Int,
                parallelism: kdf["parallelism"] as! Int,
                outputLen: kdf["outputLen"] as! Int
            )
            XCTAssertEqual(derived, Vectors.bytes(v, "expectedHex"),
                           "argon2id vector \(Vectors.str(v, "name")) mismatch")
        }
    }

    func testAesGcmVectorsRoundTrip() throws {
        for v in try Vectors.vectors("aes-gcm.json") {
            let key = Vectors.bytes(v, "keyHex")
            let nonce = Vectors.bytes(v, "nonceHex")
            let aad = Vectors.bytes(v, "aadHex")
            let plain = Vectors.bytes(v, "plaintextHex")
            let expectedCipher = Vectors.bytes(v, "cipherHex")
            let expectedTag = Vectors.bytes(v, "tagHex")

            let (cipher, tag) = try Crypto.aesGcmEncrypt(key: key, nonce: nonce,
                                                         aad: aad, plaintext: plain)
            XCTAssertEqual(cipher, expectedCipher, "cipher \(Vectors.str(v, "name"))")
            XCTAssertEqual(tag, expectedTag, "tag \(Vectors.str(v, "name"))")

            let decrypted = try Crypto.aesGcmDecrypt(key: key, nonce: nonce, aad: aad,
                                                     ciphertext: cipher, tag: tag)
            XCTAssertEqual(decrypted, plain, "decrypt \(Vectors.str(v, "name"))")
        }
    }

    func testAesGcmRejectsTamperedTag() throws {
        let v = try Vectors.vectors("aes-gcm.json")[0]
        var tag = Vectors.bytes(v, "tagHex")
        tag[0] ^= 1
        XCTAssertThrowsError(try Crypto.aesGcmDecrypt(
            key: Vectors.bytes(v, "keyHex"), nonce: Vectors.bytes(v, "nonceHex"),
            aad: Vectors.bytes(v, "aadHex"), ciphertext: Vectors.bytes(v, "cipherHex"),
            tag: tag
        )) { error in
            XCTAssertEqual(error as? GCMError, .authenticationFailed)
        }
    }

    /// SHA-256 / MGF1 自检（RSA-OAEP 的地基）：与向量固定密钥的解密结果互证。
    func testSha256KnownAnswer() {
        // SHA256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
        let d = sha256(Array("abc".utf8))
        XCTAssertEqual(Crypto.hex(d),
                       "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
    }

    /// 解密方向：桌面端生成的固定 PKCS#8 密钥必须解出同一 dataKey（钉死 DER 解析与
    /// OAEP-SHA256/MGF1-SHA256 参数）。
    func testRsaOaepDecryptsWrappedDataKey() throws {
        // rsa-oaep.json 的字段全部在 vectors[0]（顶层只有 description/note/vectors）
        let v = try Vectors.vectors("rsa-oaep.json")[0]
        XCTAssertEqual(Vectors.str(v, "oaepHash"), "sha256")
        XCTAssertEqual(Vectors.str(v, "mgfHash"), "sha256")

        let pem = Vectors.str(v, "keyPemPrivate")
        let base64 = pem.split(separator: "\n")
            .filter { !$0.contains("-----") }
            .joined()
        let privateDer = base64Bytes(base64)
        let wrapped = Vectors.bytes(v, "wrappedHex")
        XCTAssertEqual(wrapped.count, v["wrappedLen"] as! Int)
        let dataKey = try Crypto.rsaOaepDecrypt(privatePkcs8: privateDer, wrapped: wrapped)
        XCTAssertEqual(dataKey, Vectors.bytes(v, "dataKeyHex"), "decrypted data key mismatch")
    }

    /// 自洽往返 + 尺寸上限（封装方向带随机种子，不可跨端比对）。
    func testRsaOaepEncryptDecryptRoundTripAndBounds() throws {
        let v = try Vectors.vectors("rsa-oaep.json")[0]
        let pubPem = Vectors.str(v, "keyPemPublic")
        let privPem = Vectors.str(v, "keyPemPrivate")
        let publicDer = base64Bytes(pubPem.split(separator: "\n")
            .filter { !$0.contains("-----") }.joined())
        let privateDer = base64Bytes(privPem.split(separator: "\n")
            .filter { !$0.contains("-----") }.joined())

        let plain: [UInt8] = Array(0..<190)
        let wrapped = try Crypto.rsaOaepEncrypt(publicSpki: publicDer, plaintext: plain)
        XCTAssertEqual(wrapped.count, 256, "wrappedLen")
        let back = try Crypto.rsaOaepDecrypt(privatePkcs8: privateDer, wrapped: wrapped)
        XCTAssertEqual(back, plain, "roundtrip")

        XCTAssertThrowsError(try Crypto.rsaOaepEncrypt(publicSpki: publicDer,
                                                       plaintext: [UInt8](repeating: 0, count: 191))) {
            error in
            XCTAssertEqual(error as? RsaError, .plaintextTooLong, "191 字节必须被拒绝")
        }
    }

    /// 自生成密钥的 PKCS#8/SPKI 解析自洽（跨端互通的前提：写得对才能被桌面读）。
    func testGeneratedKeyPairParsesAndDecrypts() throws {
        let (privDER, pubDER) = try Crypto.rsaGenerateKeyPair()
        let plain = Array("swift-rsa-roundtrip".utf8)
        let wrapped = try Crypto.rsaOaepEncrypt(publicSpki: pubDER, plaintext: plain)
        XCTAssertEqual(wrapped.count, 256)
        let back = try Crypto.rsaOaepDecrypt(privatePkcs8: privDER, wrapped: wrapped)
        XCTAssertEqual(back, plain)
    }
}

/// PEM 中的 base64 直接解码为字节（Foundation 可解）。
func base64Bytes(_ b64: String) -> [UInt8] {
    guard let data = Data(base64Encoded: b64, options: .ignoreUnknownCharacters) else {
        XCTFail("bad base64 in PEM")
        return []
    }
    return [UInt8](data)
}
