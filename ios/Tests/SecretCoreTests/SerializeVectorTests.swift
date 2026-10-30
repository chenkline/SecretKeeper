import XCTest
@testable import SecretCore

/// 序列化层黄金向量测试：与桌面端（tests/core/serialize_test.cpp）、
/// Android（SerializeVectorTest）跑同一份数据。
final class SerializeVectorTests: XCTestCase {
    func testContainerRoundtripMatchesDesktop() throws {
        let root = try Vectors.json("container-roundtrip.json")

        // --- 主密钥文件 ---
        let mkRoot = root["masterKeyFile"] as! [String: Any]
        let mkBytes = Crypto.unhex(Vectors.str(mkRoot, "fileHex").lowercased())
        let expected = mkRoot["expected"] as! [String: Any]
        guard case .ok(let f) = Serialize.parseMasterKey(mkBytes) else {
            return XCTFail("master key file parsed")
        }
        XCTAssertEqual(Int(f.mkAlg), 1, "mkAlg == 1 (RSA-2048)")
        XCTAssertEqual(Ids.toHex(f.masterKeyId), Vectors.str(expected, "masterKeyIdHex"),
                       "masterKeyId matches vector")
        XCTAssertEqual(f.name, Vectors.str(expected, "name"), "name matches vector")
        XCTAssertEqual(Ids.toHex(f.salt), Vectors.str(expected, "saltHex"), "salt matches vector")
        XCTAssertEqual(f.kdfMem, 10240, "kdfMem")
        XCTAssertEqual(f.kdfIter, 3, "kdfIter")
        XCTAssertEqual(f.kdfPar, 1, "kdfPar")
        XCTAssertEqual(Ids.toHex(f.pubKeyNonce), Vectors.str(expected, "pubKeyNonceHex"))
        XCTAssertEqual(Ids.toHex(f.privKeyNonce), Vectors.str(expected, "privKeyNonceHex"))
        XCTAssertNotEqual(f.pubKeyNonce, f.privKeyNonce, "pub/priv nonces differ")
        XCTAssertEqual(Serialize.serializeMasterKey(f), mkBytes,
                       "master key roundtrip is byte-identical")

        // --- 机密信息文件 ---
        let sfRoot = root["secretFile"] as! [String: Any]
        let sfBytes = Crypto.unhex(Vectors.str(sfRoot, "fileHex").lowercased())
        let sExp = sfRoot["expected"] as! [String: Any]
        guard case .ok(let s) = Serialize.parseSecret(sfBytes) else {
            return XCTFail("secret file parsed")
        }
        XCTAssertEqual(Ids.toHex(s.secretId), Vectors.str(sExp, "secretIdHex"))
        XCTAssertEqual(Ids.toHex(s.masterKeyId), Vectors.str(sExp, "masterKeyIdHex"))
        XCTAssertEqual(s.title, Vectors.str(sExp, "title"))
        XCTAssertEqual(s.wrappedDk.count, 256, "wrappedDk length == 256")
        XCTAssertEqual(Ids.toHex(s.dataNonce), Vectors.str(sExp, "dataNonceHex"))
        XCTAssertEqual(Serialize.serializeSecret(s), sfBytes,
                       "secret roundtrip is byte-identical")
    }

    /// 负例：畸形输入必须返回指定错误类别。规则与桌面一致——双解析器
    /// 任一命中即通过；GCM 类向量（无 inputHex）走认证失败路径。
    func testNegativeCasesRejectedWithExpectedCategory() throws {
        let vectors = try Vectors.vectors("negative-cases.json")
        XCTAssertGreaterThanOrEqual(vectors.count, 10, "negative case count >= 10")
        var checked = 0
        for v in vectors {
            let name = Vectors.str(v, "name")
            let expect = Vectors.str(v, "expectedError")
            if v["keyHex"] != nil {
                // GCM 认证类向量：密文/口令错 -> 认证失败
                XCTAssertThrowsError(try Crypto.aesGcmDecrypt(
                    key: Vectors.bytes(v, "keyHex"), nonce: Vectors.bytes(v, "nonceHex"),
                    aad: Vectors.bytes(v, "aadHex"), ciphertext: Vectors.bytes(v, "cipherHex"),
                    tag: Vectors.bytes(v, "tagHex")
                ), name) { _ in }
                checked += 1
                continue
            }
            let bytes = Crypto.unhex(Vectors.str(v, "inputHex").lowercased())
            var e1 = Serialize.ParseError.ok
            var e2 = Serialize.ParseError.ok
            if case .err(let e) = Serialize.parseMasterKey(bytes) { e1 = e }
            if case .err(let e) = Serialize.parseSecret(bytes) { e2 = e }
            XCTAssertTrue(e1 != .ok || e2 != .ok, "\(name): must be rejected")
            // 只要有一条路径报出期望的错误类别即算通过（与桌面测试一致）
            XCTAssertEqual(e1.wireName == expect || e2.wireName == expect, true,
                           "\(name): error category should be \(expect) (got \(e1) / \(e2))")
            checked += 1
        }
        XCTAssertGreaterThanOrEqual(checked, 11, "at least 11 cases executed")
    }

    /// 额外自检：改 name 后重序列化再解析回读。
    func testParsedObjectsRoundTripIndependently() throws {
        let root = try Vectors.json("container-roundtrip.json")
        let mkBytes = Crypto.unhex((root["masterKeyFile"] as! [String: Any])["fileHex"] as! String)
        guard case .ok(let f) = Serialize.parseMasterKey(mkBytes) else {
            return XCTFail("parse")
        }
        let renamed = Serialize.MasterKeyFile(
            mkAlg: f.mkAlg, masterKeyId: f.masterKeyId, name: "renamed-key", salt: f.salt,
            kdfMem: f.kdfMem, kdfIter: f.kdfIter, kdfPar: f.kdfPar,
            pubKeyEncAlg: f.pubKeyEncAlg, pubKeyNonce: f.pubKeyNonce, pubKeyTag: f.pubKeyTag,
            pubKeyCipher: f.pubKeyCipher,
            privKeyEncAlg: f.privKeyEncAlg, privKeyNonce: f.privKeyNonce, privKeyTag: f.privKeyTag,
            privKeyCipher: f.privKeyCipher
        )
        let bytes2 = Serialize.serializeMasterKey(renamed)
        guard case .ok(let back) = Serialize.parseMasterKey(bytes2) else {
            return XCTFail("reparse")
        }
        XCTAssertEqual(back.name, "renamed-key")
        XCTAssertNotEqual(bytes2, mkBytes)
        XCTAssertEqual(back.masterKeyId, f.masterKeyId)
    }
}
