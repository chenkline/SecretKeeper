import XCTest
@testable import SecretCore

/// 存储层自检：索引 CRUD + upsert 保留 created_at + 悬空引用 + 原子写 +
/// 对象级 save/load 往返。SQLite 走系统库真实现。
final class StoreTests: XCTestCase {
    private var tmpDir: String!

    override func setUpWithError() throws {
        tmpDir = NSTemporaryDirectory() + "/sk-store-test-\(UUID().uuidString)"
        try FileManager.default.createDirectory(atPath: tmpDir, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(atPath: tmpDir)
    }

    private func openDb() throws -> IndexDb {
        let db = try IndexDb(path: tmpDir + "/secret-\(UUID().uuidString).db")
        db.createSchema()
        return db
    }

    /// 从 container-roundtrip 向量取样例对象（换 ID 构造多记录）。
    private func sampleMasterKey(_ idHex: String) throws -> Serialize.MasterKeyFile {
        let root = try Vectors.json("container-roundtrip.json")
        let mkBytes = Crypto.unhex((root["masterKeyFile"] as! [String: Any])["fileHex"] as! String)
        guard case .ok(let f) = Serialize.parseMasterKey(mkBytes) else {
            throw Vectors.TestFailure("parse vector")
        }
        if Ids.toHex(f.masterKeyId) == idHex { return f }
        return Serialize.MasterKeyFile(
            mkAlg: f.mkAlg, masterKeyId: Ids.fromHex(idHex)!, name: f.name, salt: f.salt,
            kdfMem: f.kdfMem, kdfIter: f.kdfIter, kdfPar: f.kdfPar,
            pubKeyEncAlg: f.pubKeyEncAlg, pubKeyNonce: f.pubKeyNonce, pubKeyTag: f.pubKeyTag,
            pubKeyCipher: f.pubKeyCipher,
            privKeyEncAlg: f.privKeyEncAlg, privKeyNonce: f.privKeyNonce, privKeyTag: f.privKeyTag,
            privKeyCipher: f.privKeyCipher
        )
    }

    private func sampleSecret(_ idHex: String, _ mkIdHex: String) throws -> Serialize.SecretFile {
        let root = try Vectors.json("container-roundtrip.json")
        let sfBytes = Crypto.unhex((root["secretFile"] as! [String: Any])["fileHex"] as! String)
        guard case .ok(let s) = Serialize.parseSecret(sfBytes) else {
            throw Vectors.TestFailure("parse vector")
        }
        return Serialize.SecretFile(
            secretId: Ids.fromHex(idHex)!, masterKeyId: Ids.fromHex(mkIdHex)!,
            title: s.title, wrappedDk: s.wrappedDk, dataNonce: s.dataNonce,
            dataTag: s.dataTag, dataCipher: s.dataCipher
        )
    }

    func testIdHexRoundTripStrict() {
        let id = Ids.fromHex("000102030405060708090a0b0c0d0e0f")
        XCTAssertNotNil(id)
        XCTAssertEqual(Ids.toHex(id!), "000102030405060708090a0b0c0d0e0f")
        XCTAssertNil(Ids.fromHex("0011"))
        XCTAssertNil(Ids.fromHex("zz110102030405060708090a0b0c0d0e0f"))
        XCTAssertNil(Ids.fromHex("000102030405060708090a0b0c0d0e0f00"))
        XCTAssertNil(Ids.fromHex(""))
    }

    func testIndexUpsertKeepsCreatedAt() throws {
        let db = try openDb()
        let files = FileStore(dataDir: tmpDir + "/data-\(UUID().uuidString)")
        let store = MasterKeyStore(db: db, files: files)
        let mk = try sampleMasterKey("110102030405060708090a0b0c0d0e0f")
        let idHex = Ids.toHex(mk.masterKeyId)
        XCTAssertEqual(store.save(mk, "first", makeDefault: true), .ok)
        let created = db.findMasterKey(idHex)!.createdAt
        XCTAssertGreaterThan(created, 0)
        XCTAssertEqual(store.save(mk, "renamed", makeDefault: false), .ok)
        let row = db.findMasterKey(idHex)!
        XCTAssertEqual(row.name, "renamed")
        XCTAssertEqual(row.createdAt, created, "upsert 保留 created_at")
        XCTAssertEqual(db.masterKeyCount(), 1)
    }

    func testDefaultMasterKeyUnique() throws {
        let db = try openDb()
        let store = MasterKeyStore(db: db, files: FileStore(dataDir: tmpDir))
        let a = try sampleMasterKey("110102030405060708090a0b0c0d0e0f")
        let b = try sampleMasterKey("220102030405060708090a0b0c0d0e0f")
        XCTAssertEqual(store.save(a, "a", makeDefault: true), .ok)
        XCTAssertEqual(store.save(b, "b", makeDefault: false), .ok)
        XCTAssertEqual(db.defaultMasterKeyId(), Ids.toHex(a.masterKeyId))
        XCTAssertEqual(store.save(b, "b", makeDefault: true), .ok)
        XCTAssertEqual(db.defaultMasterKeyId(), Ids.toHex(b.masterKeyId))
        XCTAssertEqual(db.listMasterKeys().filter { $0.isDefault }.count, 1, "默认唯一")
    }

    func testMasterKeyLoadRoundTrip() throws {
        let db = try openDb()
        let store = MasterKeyStore(db: db, files: FileStore(dataDir: tmpDir))
        let mk = try sampleMasterKey("110102030405060708090a0b0c0d0e0f")
        // 文件与索引同名（桌面上层保证二者一致；构造时需显式同步）
        let renamed = Serialize.MasterKeyFile(
            mkAlg: mk.mkAlg, masterKeyId: mk.masterKeyId, name: "主密钥甲", salt: mk.salt,
            kdfMem: mk.kdfMem, kdfIter: mk.kdfIter, kdfPar: mk.kdfPar,
            pubKeyEncAlg: mk.pubKeyEncAlg, pubKeyNonce: mk.pubKeyNonce, pubKeyTag: mk.pubKeyTag,
            pubKeyCipher: mk.pubKeyCipher,
            privKeyEncAlg: mk.privKeyEncAlg, privKeyNonce: mk.privKeyNonce,
            privKeyTag: mk.privKeyTag, privKeyCipher: mk.privKeyCipher
        )
        XCTAssertEqual(store.save(renamed, "主密钥甲", makeDefault: true), .ok)
        let (err, loaded) = store.load(Ids.toHex(mk.masterKeyId))
        XCTAssertEqual(err, .ok)
        XCTAssertEqual(loaded!.masterKeyId, mk.masterKeyId)
        XCTAssertEqual(loaded!.name, "主密钥甲")
        XCTAssertEqual(loaded!.salt, mk.salt)
        XCTAssertEqual(loaded!.privKeyCipher, mk.privKeyCipher)
        XCTAssertEqual(store.load("330102030405060708090a0b0c0d0e0f").0, .notFound)
        XCTAssertEqual(store.load("xyz").0, .invalidArgument)
    }

    func testSecretRoundTripAndDanglingReference() throws {
        let db = try openDb()
        let store = SecretStore(db: db, files: FileStore(dataDir: tmpDir))
        let mkId = "110102030405060708090a0b0c0d0e0f"
        let secret = try sampleSecret("440102030405060708090a0b0c0d0e0f", mkId)
        XCTAssertEqual(store.save(secret), .ok)
        let (err, loaded) = store.load(Ids.toHex(secret.secretId))
        XCTAssertEqual(err, .ok)
        XCTAssertEqual(loaded!.secretId, secret.secretId)
        XCTAssertEqual(loaded!.title, secret.title)
        XCTAssertEqual(loaded!.dataCipher, secret.dataCipher)
        // 悬空引用是合法状态
        XCTAssertEqual(db.listSecretsByMasterKey(mkId).count, 1)
        XCTAssertNil(db.findMasterKey(mkId))
    }

    func testDeleteAndCount() throws {
        let db = try openDb()
        let store = SecretStore(db: db, files: FileStore(dataDir: tmpDir))
        let mkId = "110102030405060708090a0b0c0d0e0f"
        let s1 = try sampleSecret("440102030405060708090a0b0c0d0e0f", mkId)
        let s2 = try sampleSecret("550102030405060708090a0b0c0d0e0f", mkId)
        store.save(s1)
        store.save(s2)
        XCTAssertEqual(db.secretCount(), 2)
        XCTAssertEqual(db.countSecretsForMasterKey(mkId), 2)
        XCTAssertEqual(db.deleteSecret(Ids.toHex(s1.secretId)), .ok)
        XCTAssertEqual(db.deleteSecret(Ids.toHex(s1.secretId)), .notFound)
        XCTAssertEqual(db.secretCount(), 1)
        XCTAssertEqual(db.countSecretsForMasterKey(mkId), 1)
    }

    func testFileStoreAtomicWriteAndLayout() throws {
        let files = FileStore(dataDir: tmpDir + "/data")
        XCTAssertTrue(files.ensureLayout())
        XCTAssertTrue(FileManager.default.fileExists(atPath: tmpDir + "/data/keys"))
        XCTAssertTrue(FileManager.default.fileExists(atPath: tmpDir + "/data/secrets"))
        XCTAssertEqual(files.masterKeyRelPath("110102030405060708090a0b0c0d0e0f"),
                       "keys/110102030405060708090a0b0c0d0e0f.smk")
        let bytes1: [UInt8] = [1, 2, 3]
        XCTAssertTrue(files.writeAtomic("keys/test.smk", bytes1))
        XCTAssertEqual(files.read("keys/test.smk"), bytes1)
        let bytes2: [UInt8] = [9, 8, 7, 6]
        XCTAssertTrue(files.writeAtomic("keys/test.smk", bytes2))
        XCTAssertEqual(files.read("keys/test.smk"), bytes2)
        XCTAssertFalse(FileManager.default.fileExists(atPath: tmpDir + "/data/keys/test.smk.tmp"),
                       "临时文件不残留")
        XCTAssertTrue(files.remove("keys/test.smk"))
        XCTAssertNil(files.read("keys/test.smk"))
        XCTAssertTrue(files.remove("keys/test.smk"), "幂等")
    }

    func testParseErrorPropagatesAsSaveError() throws {
        let db = try openDb()
        let files = FileStore(dataDir: tmpDir + "/data")
        files.ensureLayout()
        let store = MasterKeyStore(db: db, files: files)
        let idHex = "660102030405060708090a0b0c0d0e0f"
        let mk = try sampleMasterKey(idHex)
        XCTAssertEqual(store.save(mk, "x", makeDefault: false), .ok)
        // 篡改文件字节 -> PARSE_ERROR
        let bad: [UInt8] = [0x53, 0x53, 0x43, 0x31, 0x00, 0x01, 0x00, 0x00,
                            0x00, 0x00, 0x01, 0x6e, 0x00, 0x00, 0x00, 0x10,
                            0x0f, 0x0f, 0x0f, 0x0f]
        XCTAssertTrue(files.writeAtomic("keys/\(idHex).smk", bad))
        XCTAssertEqual(store.load(idHex).0, .parseError)
        // 文件被删但索引还在 -> IO_ERROR（与桌面 store.cpp 语义一致）
        XCTAssertTrue(files.remove("keys/\(idHex).smk"))
        XCTAssertEqual(store.load(idHex).0, .ioError)
    }
}

/// 服务层自检：解锁/锁定与退避、主密钥与机密信息核心操作、限额、
/// kNeedsPassword 分支、悬空引用掩码。与桌面 service_test.cpp 关键分支对应。
final class ServiceTests: XCTestCase {
    private var tmpDir: String!

    override func setUpWithError() throws {
        tmpDir = NSTemporaryDirectory() + "/sk-service-test-\(UUID().uuidString)"
        try FileManager.default.createDirectory(atPath: tmpDir, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(atPath: tmpDir)
    }

    private func newService() throws -> Service {
        let db = try IndexDb(path: tmpDir + "/secret-\(UUID().uuidString).db")
        db.createSchema()
        let svc = Service(db: db, files: FileStore(dataDir: tmpDir + "/data"))
        XCTAssertTrue(svc.open())
        return svc
    }

    func testSecretCharCountMatchesVectors() throws {
        for v in try charCountVectors() {
            let text = String(decoding: Vectors.bytes(v, "textHex"), as: UTF8.self)
            XCTAssertEqual(Service.secretCharCount(text),
                           v["expectedCodePoints"] as! Int,
                           "char count \(Vectors.str(v, "name"))")
        }
    }

    func testCharCountLimitEnforcedBeforePassword() throws {
        let svc = try newService()
        // 未创建任何主密钥：长度校验先于选密钥与输密码（需求 3.2）
        XCTAssertEqual(svc.addSecret("t", "", "000102030405060708090a0b0c0d0e0f", []),
                       .emptySecret)
        XCTAssertEqual(svc.addSecret("t", String(repeating: "长", count: 151),
                                     "000102030405060708090a0b0c0d0e0f", []), .secretTooLong)
        let err150 = svc.addSecret("t", String(repeating: "a", count: 150),
                                   "000102030405060708090a0b0c0d0e0f", [])
        XCTAssertNotEqual(err150, .secretTooLong)
        svc.close()
    }

    func testCreateMasterKeyAndFirstIsDefault() throws {
        let svc = try newService()
        XCTAssertEqual(svc.createMasterKey("甲", Array("pw-1".utf8)), .ok)
        XCTAssertEqual(svc.listMasterKeys().count, 1)
        XCTAssertTrue(svc.listMasterKeys()[0].isDefault)
        XCTAssertEqual(svc.defaultMasterKeyId(), svc.listMasterKeys()[0].masterKeyId)
        XCTAssertEqual(svc.createMasterKey("乙", []), .passwordWrong)
        XCTAssertEqual(svc.createMasterKey("乙", Array("pw-2".utf8)), .ok)
        XCTAssertEqual(svc.listMasterKeys().filter { $0.isDefault }.count, 1)
        svc.close()
    }

    func testMasterKeyQuotaEnforced() throws {
        let svc = try newService()
        XCTAssertEqual(svc.createMasterKey("甲", Array("pw-1".utf8)), .ok)
        XCTAssertEqual(svc.createMasterKey("乙", Array("pw-2".utf8)), .ok)
        XCTAssertEqual(svc.createMasterKey("丙", Array("pw-3".utf8)),
                       .masterKeyQuotaExceeded)
        svc.close()
    }

    func testUnlockLockAndBackoff() throws {
        let svc = try newService()
        svc.createMasterKey("甲", Array("pw-1".utf8))
        let id = svc.defaultMasterKeyId()!
        XCTAssertFalse(svc.isUnlocked())

        // 密码错误：首次报 passwordWrong，计入退避
        XCTAssertEqual(svc.unlock(id, Array("bad".utf8)), .passwordWrong)
        XCTAssertEqual(svc.backoffStatus().consecutiveFailures, 1)
        XCTAssertEqual(PasswordBackoff.delayFor(1, 60), 1)
        // 退避窗口内再失败 -> rateLimited（与桌面 note_password_failure 一致）
        XCTAssertEqual(svc.unlock(id, Array("bad".utf8)), .rateLimited)
        XCTAssertEqual(svc.backoffStatus().consecutiveFailures, 2)
        XCTAssertEqual(PasswordBackoff.delayFor(2, 60), 2)
        XCTAssertEqual(PasswordBackoff.delayFor(3, 60), 4)
        XCTAssertEqual(PasswordBackoff.delayFor(10, 60), 60, "封顶 60 秒")

        // 正确密码解锁成功并清零退避
        XCTAssertEqual(svc.unlock(id, Array("pw-1".utf8)), .ok)
        XCTAssertTrue(svc.isUnlocked())
        XCTAssertEqual(svc.backoffStatus().consecutiveFailures, 0)

        // 解锁成功即触发一次轮换（需求 4.2）：再 rotate 仍成功
        XCTAssertEqual(svc.rotateKek(), .ok)

        // 锁定后清空缓存
        svc.lock()
        XCTAssertFalse(svc.isUnlocked())
        XCTAssertEqual(svc.rotateKek(), .locked)
        XCTAssertEqual(svc.addSecret("t", "content", id, []), .locked)
        svc.close()
    }

    func testAddRevealDeleteSecretRoundTrip() throws {
        let svc = try newService()
        svc.createMasterKey("甲", Array("pw-1".utf8))
        let mkId = svc.defaultMasterKeyId()!

        // 未解锁 + 空密码 + 默认主密钥 -> locked
        XCTAssertEqual(svc.addSecret("网站甲", "secret-content", mkId, []), .locked)
        XCTAssertTrue(svc.unlock(mkId, Array("pw-1".utf8)) == .ok)
        // 已解锁 + 空密码 -> 缓存 KEK 成功
        XCTAssertEqual(svc.addSecret("网站甲", "secret-content", mkId, []), .ok)

        let list = svc.listSecrets()
        XCTAssertEqual(list.count, 1)
        XCTAssertEqual(list[0].title, "网站甲")
        XCTAssertTrue(list[0].masterKeyFound)

        // 详情：默认主密钥已解锁，空密码也可解密
        let (e, detail) = svc.getSecretDetail(list[0].secretId, [])
        XCTAssertEqual(e, .ok)
        XCTAssertNotNil(detail)
        XCTAssertFalse(detail!.plaintextMasked)
        XCTAssertEqual(detail!.plaintext, "secret-content")
        XCTAssertTrue(detail!.masterKeyFound)

        XCTAssertEqual(svc.deleteSecret(list[0].secretId, []), .ok)
        XCTAssertEqual(svc.listSecrets().count, 0)
        XCTAssertEqual(svc.deleteSecret(list[0].secretId, []), .secretNotFound)
        svc.close()
    }

    func testSecretQuotaEnforced() throws {
        let svc = try newService()
        svc.createMasterKey("甲", Array("pw-1".utf8))
        let mkId = svc.defaultMasterKeyId()!
        let pw = Array("pw-1".utf8)
        for i in 0..<ServiceLimits.maxSecrets {
            XCTAssertEqual(svc.addSecret("条目 \(i)", "content-\(i)", mkId, pw), .ok)
        }
        XCTAssertEqual(svc.addSecret("超出", "content-x", mkId, pw), .secretQuotaExceeded)
        svc.close()
    }

    func testSwitchDefaultRequiresBothPasswords() throws {
        let svc = try newService()
        svc.createMasterKey("甲", Array("pw-1".utf8))
        let first = svc.defaultMasterKeyId()!
        svc.createMasterKey("乙", Array("pw-2".utf8))
        let second = svc.listMasterKeys().first { $0.masterKeyId != first }!.masterKeyId

        // 未解锁 + 两密码齐全 -> 成功
        XCTAssertEqual(svc.switchDefaultMasterKey(second, Array("pw-1".utf8),
                                                  Array("pw-2".utf8)), .ok)
        XCTAssertEqual(svc.defaultMasterKeyId(), second)
        XCTAssertFalse(svc.isUnlocked())

        // 原默认密码错误 -> 拒绝
        XCTAssertEqual(svc.switchDefaultMasterKey(first, Array("bad".utf8),
                                                  Array("pw-1".utf8)), .passwordWrong)

        // 解锁后空密码切换可用缓存 KEK 校验
        XCTAssertEqual(svc.unlock(first, Array("pw-1".utf8)), .ok)
        XCTAssertEqual(svc.switchDefaultMasterKey(second, [], []), .ok)
        XCTAssertEqual(svc.defaultMasterKeyId(), second)
        svc.close()
    }

    func testDeleteDefaultMasterKeyForbidden() throws {
        let svc = try newService()
        svc.createMasterKey("甲", Array("pw-1".utf8))
        let first = svc.defaultMasterKeyId()!
        XCTAssertEqual(svc.deleteMasterKey(first, Array("pw-1".utf8)), .cannotDeleteDefault)
        svc.close()
    }

    func testDanglingMasterKeyMasksPlaintext() throws {
        let svc = try newService()
        svc.createMasterKey("甲", Array("pw-1".utf8))
        let mkId = svc.defaultMasterKeyId()!
        let pw = Array("pw-1".utf8)
        XCTAssertEqual(svc.addSecret("网站乙", "topsecret", mkId, pw), .ok)
        let secretId = svc.listSecrets()[0].secretId

        // 删除主密钥前先切走默认（默认不可删）
        svc.createMasterKey("乙", Array("pw-2".utf8))
        let second = svc.listMasterKeys().first { $0.masterKeyId != mkId }!.masterKeyId
        XCTAssertEqual(svc.switchDefaultMasterKey(second, Array("pw-1".utf8),
                                                  Array("pw-2".utf8)), .ok)
        XCTAssertEqual(svc.deleteMasterKey(mkId, Array("pw-1".utf8)), .ok)

        // 机密信息保留（需求 2.6），主密钥缺失 -> 掩码态 + 黄色问号
        let list = svc.listSecrets()
        XCTAssertEqual(list.count, 1)
        XCTAssertFalse(list[0].masterKeyFound)
        XCTAssertEqual(list[0].masterKeyName, "")
        let (e, detail) = svc.getSecretDetail(secretId, [])
        XCTAssertEqual(e, .ok)
        XCTAssertNotNil(detail)
        XCTAssertTrue(detail!.plaintextMasked)
        XCTAssertFalse(detail!.masterKeyFound)
        XCTAssertEqual(Service.maskedPlaintext, "******")
        svc.close()
    }

    func testExportImportMasterKeyRoundTrip() throws {
        let svc = try newService()
        svc.createMasterKey("甲", Array("pw-1".utf8))
        let id = svc.defaultMasterKeyId()!
        XCTAssertEqual(svc.unlock(id, Array("pw-1".utf8)), .ok)

        let (e, bytes) = svc.exportMasterKey(id, [], Array("prot-9".utf8))
        XCTAssertEqual(e, .ok)
        XCTAssertNotNil(bytes)
        // 导入到独立库验证
        let tmp2 = NSTemporaryDirectory() + "/sk-import-\(UUID().uuidString)"
        try FileManager.default.createDirectory(atPath: tmp2, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(atPath: tmp2) }
        let db2 = try IndexDb(path: tmp2 + "/secret.db")
        db2.createSchema()
        let svc2 = Service(db: db2, files: FileStore(dataDir: tmp2 + "/data"))
        XCTAssertTrue(svc2.open())
        XCTAssertEqual(svc2.importMasterKey(bytes!, Array("prot-9".utf8),
                                            Array("new-pw".utf8)), .ok)
        XCTAssertEqual(svc2.listMasterKeys().count, 1)
        XCTAssertNil(svc2.defaultMasterKeyId(), "导入的密钥不自动成为默认")
        let importedId = svc2.listMasterKeys()[0].masterKeyId
        XCTAssertEqual(svc2.unlock(importedId, Array("new-pw".utf8)), .ok)
        svc2.close()
        svc.close()
    }
}
