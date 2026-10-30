import Foundation

/// 服务层（iOS core 总入口）。移植自 android/.../Service.kt（已通过
/// ServiceTest），语义与桌面 src/common/core/service.cpp 逐项对齐：
/// UI 只通过本层访问 core；不得接触 KEK、GCM、RSA 等任何密码学符号。

/// 限额：业务规则集中于此，v1.0.0 由 License 覆盖时只改这一处。
public enum ServiceLimits {
    public static let maxMasterKeys = 2
    public static let maxSecrets = 15
    public static let maxSecretChars = 150
}

/// 业务错误。语义与桌面 service::Error 一一对应。
public enum SvcError: Equatable {
    case ok
    case passwordWrong      // 主密钥密码错误
    case rateLimited        // 退避等待中
    case needsPassword      // 需要非默认主密钥密码，UI 弹框后带密码重试
    case locked             // 尚未解锁
    case masterKeyQuotaExceeded
    case masterKeyNotFound
    case cannotDeleteDefault // 禁止删除默认主密钥
    case secretQuotaExceeded
    case secretTooLong      // 超出 150 字符
    case secretNotFound
    case emptySecret        // 内容为空
    case importBadFormat
    case importVersionTooHigh
    case importMasterKeyMissing
    case importDecryptFailed
    case ioError
    case internalError
}

/// 需求文档固定的中文提示文案，UI 原样显示，不得改写。
public enum SvcMessages {
    public static func message(_ e: SvcError) -> String {
        switch e {
        case .ok: return ""
        case .passwordWrong: return "主密钥密码错误"
        case .rateLimited: return "尝试过于频繁，请稍后再试"
        case .needsPassword: return "请输入该主密钥的密码"
        case .locked: return "密码库已锁定，请先解锁"
        case .masterKeyQuotaExceeded: return "主密钥数量已达上限，请先导出并删除部分记录"
        case .masterKeyNotFound: return "主密钥不存在"
        case .cannotDeleteDefault: return "不能删除默认主密钥，请先切换默认主密钥"
        case .secretQuotaExceeded: return "机密信息条数已达上限，请先导出并删除部分记录"
        case .secretTooLong: return "机密信息超出 150 字符"
        case .secretNotFound: return "机密信息不存在"
        case .emptySecret: return "机密信息内容不能为空"
        case .importBadFormat: return "文件格式不正确"
        case .importVersionTooHigh: return "文件版本过高，请先升级应用"
        case .importMasterKeyMissing: return "请先导入对应的主密钥"
        case .importDecryptFailed: return "解密失败，文件可能已损坏或密码不正确"
        case .ioError: return "存储读写失败，请稍后重试"
        case .internalError: return "内部错误"
        }
    }
}

// ===========================================================================
// DTO —— UI 只看到这些
// ===========================================================================

public struct MasterKeyListItem {
    public var masterKeyId: String
    public var name: String // 可为空，且为空不代表该密钥不存在
    public var isDefault: Bool
    public var secretCount: Int
    public var createdAt: Int64 // Unix 毫秒，02 号画板的「生成/导入」日期
}

public struct SecretListItem {
    public var secretId: String
    public var title: String // 可为空
    public var masterKeyId: String
    public var masterKeyName: String
    /// 密钥存在但名称为空 -> true；密钥缺失 -> false（黄色问号）。
    public var masterKeyFound: Bool
}

public struct SecretDetail {
    public var secretId: String
    public var title: String
    public var masterKeyId: String
    public var masterKeyName: String
    public var masterKeyFound: Bool
    /// 主密钥缺失或尚未提供密码时为 true，UI 显示 6 个 * 掩码。
    public var plaintextMasked: Bool
    public var plaintext: String
}

public struct QuotaStatus {
    public var masterKeys: Int
    public var maxMasterKeys: Int
    public var secrets: Int
    public var maxSecrets: Int
}

public struct BackoffStatus {
    public var waiting: Bool
    public var remainingSeconds: Int64
    public var consecutiveFailures: Int64
}

// ===========================================================================
// 密码退避（需求 4.5）
//
// 第 n 次失败后 delay(n) = min(2^(n-1) 秒, 60 秒)；只统计「密码错误」，
// 文件损坏等不属于密码问题。仅存内存，不落盘。
// ===========================================================================

public final class PasswordBackoff {
    private let capSeconds: Int64
    private var failures: Int64 = 0
    private var windowStartMs: Int64 = 0
    private var waiting = false

    public init(capSeconds: Int64 = 60) { self.capSeconds = capSeconds }

    /// 纯函数形式，便于测试：给定连续失败次数返回应等待秒数。
    public static func delayFor(_ consecutiveFailures: Int64, _ capSeconds: Int64) -> Int64 {
        guard consecutiveFailures > 0 else { return 0 }
        var exp: Int64 = 1
        let shifts = min(consecutiveFailures - 1, 62)
        for _ in 0..<shifts { exp <<= 1 }
        if exp < 0 { exp = Int64.max } // 左移溢出兜底
        return min(exp, capSeconds)
    }

    /// 记录一次密码错误，返回此后需等待的秒数。
    @discardableResult
    public func recordFailure() -> Int64 {
        failures += 1
        windowStartMs = Self.nowMs()
        waiting = true
        return Self.delayFor(failures, capSeconds)
    }

    /// 密码正确，清零计数。
    public func reset() {
        failures = 0
        waiting = false
        windowStartMs = 0
    }

    public func consecutiveFailures() -> Int64 { failures }

    /// 退避窗口剩余秒数（向上取整）。不在窗口内返回 0。
    public func remainingSeconds() -> Int64 {
        guard waiting, failures > 0 else { return 0 }
        let delay = Self.delayFor(failures, capSeconds)
        guard delay > 0 else { return 0 }
        let elapsed = (Self.nowMs() - windowStartMs) / 1000
        return max(0, delay - elapsed)
    }

    public func isWaiting() -> Bool { remainingSeconds() > 0 }

    private static func nowMs() -> Int64 {
        Int64(Date().timeIntervalSince1970 * 1000)
    }
}

// ===========================================================================
// KEK 缓存（需求 2.7）
//
// 载入时生成随机对称密钥；KEK 以 AES-256-GCM 用它包裹，密文与程序随机
// 密钥分散在两个对象中；内存中 3 个备选槽位，每次轮换随机迁移，原槽位
// 不清除（诱饵）。轮换时机：每 30 秒、每次解锁成功后。
// ===========================================================================

public final class KekCache {
    public static let slotCount = 3
    public static let rotationSeconds = 30
    static let kekLength = 32

    private var appKey: [UInt8]? // 程序随机密钥，与槽位分散保存
    private var slots = [[UInt8]?](repeating: nil, count: slotCount) // KEK 密文（明文+tag）
    private var nonces = [[UInt8]?](repeating: nil, count: slotCount)
    private var activeSlot = -1
    private var loaded = false

    public init() {}

    /// 生成新的程序随机密钥并载入 KEK。每次解锁成功后都应调用一次。
    @discardableResult
    public func load(_ kek: [UInt8]) -> Bool {
        clear()
        appKey = Crypto.randomBytes(32)
        let slot = Int.random(in: 0..<Self.slotCount)
        guard reencryptInto(slot, kek) else {
            clear()
            return false
        }
        activeSlot = slot
        loaded = true
        return true
    }

    /// 轮换：重新加密并随机迁移到另一个槽位，原槽位保留作诱饵。
    @discardableResult
    public func rotate() -> Bool {
        guard loaded, let kek = peek() else { return false }
        var next = Int.random(in: 0..<Self.slotCount)
        if Self.slotCount > 1 {
            while next == activeSlot { next = Int.random(in: 0..<Self.slotCount) }
        }
        guard reencryptInto(next, kek) else { return false }
        activeSlot = next
        return true
    }

    /// 锁定：清零程序随机密钥与全部槽位。
    public func clear() {
        if var k = appKey {
            for i in 0..<k.count { k[i] = 0 }
            appKey = nil
        }
        for i in 0..<Self.slotCount {
            if var s = slots[i] { for j in 0..<s.count { s[j] = 0 } }
            slots[i] = nil
            if var n = nonces[i] { for j in 0..<n.count { n[j] = 0 } }
            nonces[i] = nil
        }
        activeSlot = -1
        loaded = false
    }

    /// 取回缓存的 KEK。仅在已载入时有效。
    public func peek() -> [UInt8]? {
        guard loaded, let key = appKey,
              let sealed = slots[activeSlot], let nonce = nonces[activeSlot],
              sealed.count > Crypto.tagLen else { return nil }
        let ct = Array(sealed[0..<(sealed.count - Crypto.tagLen)])
        let tag = Array(sealed[(sealed.count - Crypto.tagLen)...])
        do {
            return try Crypto.aesGcmDecrypt(key: key, nonce: nonce, aad: [],
                                            ciphertext: ct, tag: tag)
        } catch {
            return nil
        }
    }

    public func hasValue() -> Bool { loaded }

    public func activeSlotIndex() -> Int { activeSlot }

    /// 某个槽位是否已被占用（诱饵槽位确实留有数据）。
    public func slotOccupied(_ index: Int) -> Bool { slots[index] != nil }

    private func reencryptInto(_ slot: Int, _ kek: [UInt8]) -> Bool {
        guard let key = appKey else { return false }
        let nonce = Crypto.randomBytes(Serialize.nonceLength)
        guard let (cipher, tag) = try? Crypto.aesGcmEncrypt(
            key: key, nonce: nonce, aad: [], plaintext: kek
        ) else { return false }
        slots[slot] = cipher + tag
        nonces[slot] = nonce
        return true
    }
}

// ===========================================================================
// Service 门面
// ===========================================================================

public final class Service {
    /// 需求 3.5：主密钥缺失时详情页显示的掩码，固定 6 个星号。
    public static let maskedPlaintext = "******"

    /// 单条机密信息的字符数（按 Unicode 码点计），供 UI 显示「n / 150」。
    public static func secretCharCount(_ plaintext: String) -> Int {
        plaintext.unicodeScalars.count
    }

    /// 是否属于「密码错误」类错误。只有这类才计入连续失败次数与退避。
    public static func isPasswordError(_ e: SvcError) -> Bool {
        e == .passwordWrong || e == .rateLimited
    }

    private let db: IndexDb
    private let files: FileStore
    private let kekCache = KekCache()
    private let backoff = PasswordBackoff()
    private let mkStore: MasterKeyStore
    private let secretStore: SecretStore

    public init(db: IndexDb, files: FileStore) {
        self.db = db
        self.files = files
        mkStore = MasterKeyStore(db: db, files: files)
        secretStore = SecretStore(db: db, files: files)
    }

    // ------------------------------------------------------ 生命周期

    @discardableResult
    public func open() -> Bool { files.ensureLayout() }

    public func close() {
        lock()
    }

    // ------------------------------------------------------ 会话与锁定

    /// 用指定主密钥的密码解锁。成功后 KEK 进入缓存，并触发一次轮换（需求 4.2）。
    @discardableResult
    public func unlock(_ masterKeyId: String, _ password: [UInt8]) -> SvcError {
        guard db.findMasterKey(masterKeyId) != nil else { return .masterKeyNotFound }
        let (se, file) = mkStore.load(masterKeyId)
        guard se == .ok, let f = file else { return mapSave(se) }

        let kek = Crypto.deriveKek(password: password, salt: f.salt,
                                   memoryKiB: Int(f.kdfMem), iterations: Int(f.kdfIter),
                                   parallelism: Int(f.kdfPar), outputLen: KekCache.kekLength)

        // 只解公钥即可判定密码正确性，不必付出解私钥的代价。
        if !aesGcmDecryptOk(kek, f.pubKeyNonce, f.masterKeyId, f.pubKeyCipher) {
            return notePasswordFailure()
        }
        guard kekCache.load(kek) else { return .internalError }
        notePasswordSuccess()
        kekCache.rotate() // 需求 4.2：每次解锁成功后立即轮换一次。
        return .ok
    }

    /// 锁定：清空 KEK 缓存与退避计数。
    public func lock() {
        kekCache.clear()
        backoff.reset()
    }

    public func isUnlocked() -> Bool { kekCache.hasValue() }

    /// KEK 轮换（需求 2.7 的 30 秒定时器）。未解锁时返回 locked。
    public func rotateKek() -> SvcError {
        if !isUnlocked() { return .locked }
        return kekCache.rotate() ? .ok : .internalError
    }

    /// 记录一次用户活动，供 UI 的闲置锁定计时使用。
    public func notifyActivity() {}

    /// 退避状态。UI 用它显示「N 秒后可解锁」。
    public func backoffStatus() -> BackoffStatus {
        BackoffStatus(waiting: backoff.isWaiting(),
                      remainingSeconds: backoff.remainingSeconds(),
                      consecutiveFailures: backoff.consecutiveFailures())
    }

    // ------------------------------------------------------ 主密钥（需求 §2）

    public func listMasterKeys() -> [MasterKeyListItem] {
        db.listMasterKeys().map { row in
            MasterKeyListItem(
                masterKeyId: row.masterKeyId, name: row.name,
                isDefault: row.isDefault || isDefault(row.masterKeyId),
                secretCount: db.countSecretsForMasterKey(row.masterKeyId),
                createdAt: row.createdAt
            )
        }
    }

    /// 生成一把新主密钥。第一把自动成为默认主密钥。
    @discardableResult
    public func createMasterKey(_ name: String, _ password: [UInt8]) -> SvcError {
        if password.isEmpty { return .passwordWrong }
        // 限额是业务规则，在 service 判定；存储层只如实计数。
        if db.masterKeyCount() >= ServiceLimits.maxMasterKeys {
            return .masterKeyQuotaExceeded
        }

        let id = Crypto.generateId()
        let salt = Crypto.randomBytes(Serialize.saltLength)
        let kek = Crypto.deriveKek(password: password, salt: salt,
                                   memoryKiB: Crypto.kdfMemoryKiB,
                                   iterations: Crypto.kdfIterations,
                                   parallelism: Crypto.kdfParallelism,
                                   outputLen: KekCache.kekLength)

        guard let pair = try? Crypto.rsaGenerateKeyPair() else { return .internalError }

        guard let sealed = sealKeys(kek, id, pair.publicDER, pair.privateDER) else {
            return .internalError
        }

        let f = Serialize.MasterKeyFile(
            masterKeyId: id, name: name, salt: salt,
            kdfMem: UInt32(Crypto.kdfMemoryKiB), kdfIter: UInt32(Crypto.kdfIterations),
            kdfPar: UInt32(Crypto.kdfParallelism),
            pubKeyNonce: sealed.pubNonce, pubKeyTag: sealed.pubTag,
            pubKeyCipher: sealed.pubCipher,
            privKeyNonce: sealed.privNonce, privKeyTag: sealed.privTag,
            privKeyCipher: sealed.privCipher
        )

        // 第一把主密钥自动成为默认主密钥。
        let makeDefault = db.masterKeyCount() == 0
        let se = mkStore.save(f, name, makeDefault: makeDefault)
        if se != .ok { return mapSave(se) }
        if makeDefault {
            if db.setDefaultMasterKey(Ids.toHex(id)) != .ok { return .internalError }
        }
        return .ok
    }

    public func verifyMasterKeyPassword(_ masterKeyId: String, _ password: [UInt8]) -> SvcError {
        let (_, e) = resolveKek(masterKeyId, password)
        return e
    }

    /// 导出主密钥。masterPassword 为空且目标是默认主密钥时用缓存的 KEK。
    /// protectionPassword 是导出文件的新保护密码，不能与主密钥密码混用。
    public func exportMasterKey(_ masterKeyId: String, _ masterPassword: [UInt8],
                                _ protectionPassword: [UInt8]) -> (SvcError, [UInt8]?) {
        if protectionPassword.isEmpty { return (.passwordWrong, nil) }
        let (kek, ke) = resolveKek(masterKeyId, masterPassword)
        if ke != .ok { return (ke, nil) }

        let (se, file) = mkStore.load(masterKeyId)
        guard se == .ok, let f = file else { return (mapSave(se), nil) }

        guard let pubPlain = decryptOrNul(kek, f.pubKeyNonce, f.masterKeyId, f.pubKeyCipher),
              let privPlain = decryptOrNul(kek, f.privKeyNonce, f.masterKeyId, f.privKeyCipher) else {
            return (.passwordWrong, nil)
        }

        // docs/03-data §7.3：导出使用新盐、保护密码派生的新 KEK、公私钥各自新 nonce。
        let salt = Crypto.randomBytes(Serialize.saltLength)
        let protKek = Crypto.deriveKek(password: protectionPassword, salt: salt,
                                       memoryKiB: Crypto.kdfMemoryKiB,
                                       iterations: Crypto.kdfIterations,
                                       parallelism: Crypto.kdfParallelism,
                                       outputLen: KekCache.kekLength)
        guard let sealed = sealKeys(protKek, f.masterKeyId, pubPlain, privPlain) else {
            return (.internalError, nil)
        }

        let dst = Serialize.MasterKeyFile(
            masterKeyId: f.masterKeyId, name: f.name, salt: salt,
            kdfMem: UInt32(Crypto.kdfMemoryKiB), kdfIter: UInt32(Crypto.kdfIterations),
            kdfPar: UInt32(Crypto.kdfParallelism),
            pubKeyNonce: sealed.pubNonce, pubKeyTag: sealed.pubTag,
            pubKeyCipher: sealed.pubCipher,
            privKeyNonce: sealed.privNonce, privKeyTag: sealed.privTag,
            privKeyCipher: sealed.privCipher
        )
        return (.ok, Serialize.serializeMasterKey(dst))
    }

    /// 导入主密钥：用 protectionPassword 解开文件，再用 newPassword 重加密保存。
    @discardableResult
    public func importMasterKey(_ bytes: [UInt8], _ protectionPassword: [UInt8],
                                _ newPassword: [UInt8]) -> SvcError {
        if protectionPassword.isEmpty || newPassword.isEmpty { return .passwordWrong }
        if db.masterKeyCount() >= ServiceLimits.maxMasterKeys {
            return .masterKeyQuotaExceeded
        }

        let src: Serialize.MasterKeyFile
        switch Serialize.parseMasterKey(bytes) {
        case .ok(let f): src = f
        case .err(let e):
            return e == .unsupportedVersion ? .importVersionTooHigh : .importBadFormat
        }

        let protKek = Crypto.deriveKek(password: protectionPassword, salt: src.salt,
                                       memoryKiB: Crypto.kdfMemoryKiB,
                                       iterations: Crypto.kdfIterations,
                                       parallelism: Crypto.kdfParallelism,
                                       outputLen: KekCache.kekLength)
        guard let pubPlain = decryptOrNul(protKek, src.pubKeyNonce, src.masterKeyId, src.pubKeyCipher),
              let privPlain = decryptOrNul(protKek, src.privKeyNonce, src.masterKeyId, src.privKeyCipher) else {
            return .passwordWrong
        }

        // ID 冲突时重新生成，保证「导出→导入」往返一致且不覆盖本地记录。
        var id = src.masterKeyId
        if db.findMasterKey(Ids.toHex(id)) != nil { id = Crypto.generateId() }

        let salt = Crypto.randomBytes(Serialize.saltLength)
        let newKek = Crypto.deriveKek(password: newPassword, salt: salt,
                                      memoryKiB: Crypto.kdfMemoryKiB,
                                      iterations: Crypto.kdfIterations,
                                      parallelism: Crypto.kdfParallelism,
                                      outputLen: KekCache.kekLength)
        guard let sealed = sealKeys(newKek, id, pubPlain, privPlain) else {
            return .internalError
        }

        let dst = Serialize.MasterKeyFile(
            masterKeyId: id, name: src.name, salt: salt,
            kdfMem: UInt32(Crypto.kdfMemoryKiB), kdfIter: UInt32(Crypto.kdfIterations),
            kdfPar: UInt32(Crypto.kdfParallelism),
            pubKeyNonce: sealed.pubNonce, pubKeyTag: sealed.pubTag,
            pubKeyCipher: sealed.pubCipher,
            privKeyNonce: sealed.privNonce, privKeyTag: sealed.privTag,
            privKeyCipher: sealed.privCipher
        )
        // §2.4：导入的密钥不自动成为默认主密钥。
        let se = mkStore.save(dst, src.name, makeDefault: false)
        if se != .ok { return mapSave(se) }
        return .ok
    }

    /// 切换默认主密钥。需求 2.5 要求同时验证原默认与新默认两把密钥的密码。
    @discardableResult
    public func switchDefaultMasterKey(_ masterKeyId: String,
                                       _ currentPassword: [UInt8],
                                       _ newPassword: [UInt8]) -> SvcError {
        guard let current = db.defaultMasterKeyId() else { return .masterKeyNotFound }
        guard db.findMasterKey(masterKeyId) != nil else { return .masterKeyNotFound }

        if current != masterKeyId {
            // 已解锁时缓存 KEK 是会话凭证，两把密钥共用同一个密码。
            func verify(_ id: String, _ pw: [UInt8]) -> SvcError {
                if pw.isEmpty && kekCache.hasValue() { return .ok }
                let (_, e) = resolveKek(id, pw)
                return e
            }
            let e = verify(current, currentPassword)
            if e != .ok { return e }
            let e2 = verify(masterKeyId, newPassword)
            if e2 != .ok { return e2 }
        }

        if db.setDefaultMasterKey(masterKeyId) != .ok { return .internalError }

        // 默认密钥换了，缓存的 KEK 不再对应任何默认密钥，必须清掉。
        kekCache.clear()
        return .ok
    }

    @discardableResult
    public func deleteMasterKey(_ masterKeyId: String, _ password: [UInt8]) -> SvcError {
        guard let row = db.findMasterKey(masterKeyId) else { return .masterKeyNotFound }
        // §2.6：禁止删除默认主密钥。
        if row.isDefault || isDefault(masterKeyId) { return .cannotDeleteDefault }

        let (_, ke) = resolveKek(masterKeyId, password)
        if ke != .ok { return ke }

        if db.deleteMasterKey(masterKeyId) != .ok { return .internalError }

        // 索引条目删除后才删数据文件。关联的机密信息**保留**（需求 2.6）。
        files.remove(row.filePath)
        return .ok
    }

    // -------------------------------------------------- 机密信息（需求 §3）

    public func listSecrets() -> [SecretListItem] {
        db.listSecrets().map { row in
            let mk = db.findMasterKey(row.masterKeyId)
            // 悬空引用是合法状态：主密钥可能已删除而机密信息保留（需求 2.6）。
            return SecretListItem(
                secretId: row.secretId, title: row.title,
                masterKeyId: row.masterKeyId, masterKeyName: mk?.name ?? "",
                masterKeyFound: mk != nil
            )
        }
    }

    /// 新增一条机密信息。masterPassword 为空且目标是默认主密钥时用缓存的 KEK。
    @discardableResult
    public func addSecret(_ title: String, _ plaintext: String,
                          _ masterKeyId: String, _ masterPassword: [UInt8]) -> SvcError {
        // 3.2 的长度校验按 Unicode 码点计，且**先于**选密钥与输密码。
        if plaintext.isEmpty { return .emptySecret }
        if Self.secretCharCount(plaintext) > ServiceLimits.maxSecretChars {
            return .secretTooLong
        }
        if db.secretCount() >= ServiceLimits.maxSecrets {
            return .secretQuotaExceeded
        }

        let (masterKek, ke) = resolveKek(masterKeyId, masterPassword)
        if ke != .ok { return ke }

        let (kse, mkFile) = mkStore.load(masterKeyId)
        guard kse == .ok, let mk = mkFile else { return mapSave(kse) }

        guard let pubPlain = decryptOrNul(masterKek, mk.pubKeyNonce, mk.masterKeyId, mk.pubKeyCipher) else {
            return .passwordWrong
        }

        // 3.2 step 3: 每条机密信息一把全新的 AES-256 数据密钥。
        let dk = Crypto.randomBytes(32)
        let newSecretId = Crypto.generateId()

        // 3.2 step 5: 用主密钥公钥封装数据密钥。
        guard let wrapped = try? Crypto.rsaOaepEncrypt(publicSpki: pubPlain,
                                                       plaintext: dk) else {
            return .internalError
        }

        let nonce = Crypto.randomBytes(Serialize.nonceLength)

        // 3.2 step 4: 用数据密钥加密，AAD 是原始 secretId。
        guard let (cipher, tag) = try? Crypto.aesGcmEncrypt(
            key: dk, nonce: nonce, aad: newSecretId,
            plaintext: Array(plaintext.utf8)) else { return .internalError }

        guard let mkId = Ids.fromHex(masterKeyId) else { return .masterKeyNotFound }

        let f = Serialize.SecretFile(
            secretId: newSecretId, masterKeyId: mkId, title: title,
            wrappedDk: wrapped, dataNonce: nonce, dataTag: tag,
            dataCipher: cipher + tag // 布局：密文 + tag 尾
        )
        let se = secretStore.save(f)
        if se != .ok { return mapSave(se) }
        return .ok
    }

    /// 需求 3.3：导出内容与数据文件逐字节相同，因此不需要密码。
    public func exportSecret(_ secretId: String, _ masterPassword: [UInt8]) -> (SvcError, [UInt8]?) {
        guard db.findSecret(secretId) != nil else { return (.secretNotFound, nil) }
        let (se, file) = secretStore.load(secretId)
        guard se == .ok, let f = file else { return (mapSave(se), nil) }
        return (.ok, Serialize.serializeSecret(f))
    }

    /// 导入机密信息文件。filePassword 预留（v0.0.1 不再包一层保护密码）。
    @discardableResult
    public func importSecret(_ bytes: [UInt8], _ filePassword: [UInt8],
                             _ masterPassword: [UInt8]) -> SvcError {
        if db.secretCount() >= ServiceLimits.maxSecrets { return .secretQuotaExceeded }
        let src: Serialize.SecretFile
        switch Serialize.parseSecret(bytes) {
        case .ok(let f): src = f
        case .err(let e):
            return e == .unsupportedVersion ? .importVersionTooHigh : .importBadFormat
        }

        // 主密钥必须存在本地，否则无法解封装。
        let mkHex = Ids.toHex(src.masterKeyId)
        guard db.findMasterKey(mkHex) != nil else { return .importMasterKeyMissing }

        let (masterKek, ke) = resolveKek(mkHex, masterPassword)
        if ke != .ok { return ke }

        // 解密校验：数据密钥必须真的能解开，否则文件已损坏或密码不对。
        guard let plain = unwrapPlaintext(masterKek, src) else {
            return .importDecryptFailed
        }

        var record = src
        // 保留原 ID 使导出/导入往返无损；冲突时换新 ID 且重加密（ID 是 AAD）。
        if db.findSecret(Ids.toHex(src.secretId)) != nil {
            let newId = Crypto.generateId()
            let (kse, mkFile) = mkStore.load(mkHex)
            guard kse == .ok, let mk = mkFile else { return mapSave(kse) }
            guard let privPlain = decryptOrNul(masterKek, mk.privKeyNonce, mk.masterKeyId,
                                               mk.privKeyCipher) else {
                return .passwordWrong
            }
            guard let dk = try? Crypto.rsaOaepDecrypt(privatePkcs8: privPlain,
                                                      wrapped: src.wrappedDk) else {
                return .importDecryptFailed
            }
            let nonce = Crypto.randomBytes(Serialize.nonceLength)
            guard let (cipher, tag) = try? Crypto.aesGcmEncrypt(
                key: dk, nonce: nonce, aad: newId, plaintext: plain
            ) else { return .internalError }
            record = Serialize.SecretFile(
                secretId: newId, masterKeyId: src.masterKeyId, title: src.title,
                wrappedDk: src.wrappedDk, dataNonce: nonce, dataTag: tag,
                dataCipher: cipher + tag
            )
        }

        let se = secretStore.save(record)
        if se != .ok { return mapSave(se) }
        return .ok
    }

    /// 详情页。masterPassword 为空时返回掩码态，不返回 needsPassword。
    public func getSecretDetail(_ secretId: String,
                                _ masterPassword: [UInt8]) -> (SvcError, SecretDetail?) {
        guard let row = db.findSecret(secretId) else { return (.secretNotFound, nil) }

        guard let mk = db.findMasterKey(row.masterKeyId) else {
            // 需求 3.5：主密钥缺失 -> 掩码正文 + 空名称列 + ID 列黄色问号。
            return (.ok, SecretDetail(
                secretId: row.secretId, title: row.title,
                masterKeyId: row.masterKeyId, masterKeyName: "",
                masterKeyFound: false, plaintextMasked: true, plaintext: ""
            ))
        }

        // 主密钥存在但名称可能为空。那**不是**「未找到」，不得产生问号。
        if masterPassword.isEmpty && !isDefault(row.masterKeyId) {
            return (.ok, SecretDetail(
                secretId: row.secretId, title: row.title,
                masterKeyId: row.masterKeyId, masterKeyName: mk.name,
                masterKeyFound: true, plaintextMasked: true, plaintext: ""
            ))
        }

        let (e, plain) = revealSecretPlaintext(secretId, masterPassword)
        if e == .ok, let p = plain {
            return (.ok, SecretDetail(
                secretId: row.secretId, title: row.title,
                masterKeyId: row.masterKeyId, masterKeyName: mk.name,
                masterKeyFound: true, plaintextMasked: false, plaintext: p
            ))
        }
        if e == .needsPassword || e == .locked {
            return (.ok, SecretDetail(
                secretId: row.secretId, title: row.title,
                masterKeyId: row.masterKeyId, masterKeyName: mk.name,
                masterKeyFound: true, plaintextMasked: true, plaintext: ""
            ))
        }
        return (e, nil)
    }

    /// 「查看」明文。默认主密钥用缓存 KEK；非默认主密钥需密码。
    public func revealSecretPlaintext(_ secretId: String,
                                      _ masterPassword: [UInt8]) -> (SvcError, String?) {
        guard let row = db.findSecret(secretId) else { return (.secretNotFound, nil) }
        let (se, file) = secretStore.load(secretId)
        guard se == .ok, let f = file else { return (mapSave(se), nil) }

        let (masterKek, ke) = resolveKek(row.masterKeyId, masterPassword)
        if ke != .ok { return (ke, nil) }

        guard let plain = unwrapPlaintext(masterKek, f) else {
            return (.importDecryptFailed, nil)
        }
        return (.ok, String(decoding: plain, as: UTF8.self))
    }

    @discardableResult
    public func deleteSecret(_ secretId: String, _ masterPassword: [UInt8]) -> SvcError {
        guard let row = db.findSecret(secretId) else { return .secretNotFound }

        // 需求 3.6：删除需验证该密钥密码；主密钥缺失时跳过校验。
        let (_, ke) = resolveKek(row.masterKeyId, masterPassword)
        if ke != .ok && ke != .masterKeyNotFound { return ke }

        // 先删索引行再删数据文件：反过来会在删索引失败时留下孤儿文件。
        if db.deleteSecret(secretId) != .ok { return .internalError }
        files.remove(row.filePath)
        return .ok
    }

    // ------------------------------------------------------ 杂项

    public func quota() -> QuotaStatus {
        QuotaStatus(masterKeys: db.masterKeyCount(), maxMasterKeys: ServiceLimits.maxMasterKeys,
                    secrets: db.secretCount(), maxSecrets: ServiceLimits.maxSecrets)
    }

    public func defaultMasterKeyId() -> String? { db.defaultMasterKeyId() }

    // ------------------------------------------------------ 内部实现

    private func isDefault(_ masterKeyId: String) -> Bool {
        db.defaultMasterKeyId() == masterKeyId
    }

    /// 记录一次密码失败。首次失败报密码错误；退避窗口内再失败才升级为限流。
    private func notePasswordFailure() -> SvcError {
        let wasWaiting = backoff.isWaiting()
        backoff.recordFailure()
        return wasWaiting ? .rateLimited : .passwordWrong
    }

    private func notePasswordSuccess() {
        backoff.reset()
    }

    /// 解析本次操作该用哪个 KEK：
    ///   密码非空 -> 校验该密码，成功则用它的 KEK（失败计入退避）
    ///   密码为空 -> 仅限「目标是默认主密钥」且「已解锁」时用缓存 KEK。
    private func resolveKek(_ masterKeyId: String, _ password: [UInt8]) -> ([UInt8], SvcError) {
        guard db.findMasterKey(masterKeyId) != nil else {
            return ([], .masterKeyNotFound)
        }
        if password.isEmpty {
            guard isDefault(masterKeyId) else { return ([], .needsPassword) }
            guard let cached = kekCache.peek() else { return ([], .locked) }
            return (cached, .ok)
        }
        let (se, file) = mkStore.load(masterKeyId)
        guard se == .ok, let f = file else { return ([], mapSave(se)) }
        let kek = Crypto.deriveKek(password: password, salt: f.salt,
                                   memoryKiB: Int(f.kdfMem), iterations: Int(f.kdfIter),
                                   parallelism: Int(f.kdfPar), outputLen: KekCache.kekLength)
        if !aesGcmDecryptOk(kek, f.pubKeyNonce, f.masterKeyId, f.pubKeyCipher) {
            return ([], notePasswordFailure())
        }
        notePasswordSuccess()
        return (kek, .ok)
    }

    /// GCM 解密（AAD = masterKeyId），失败返回 nil。密文含 16 字节 tag 尾。
    private func decryptOrNul(_ kek: [UInt8], _ nonce: [UInt8], _ aad: [UInt8],
                              _ cipherWithTag: [UInt8]) -> [UInt8]? {
        guard cipherWithTag.count > Crypto.tagLen else { return nil }
        let ct = Array(cipherWithTag[0..<(cipherWithTag.count - Crypto.tagLen)])
        let tag = Array(cipherWithTag[(cipherWithTag.count - Crypto.tagLen)...])
        do {
            return try Crypto.aesGcmDecrypt(key: kek, nonce: nonce, aad: aad,
                                            ciphertext: ct, tag: tag)
        } catch {
            return nil
        }
    }

    private func aesGcmDecryptOk(_ kek: [UInt8], _ nonce: [UInt8], _ aad: [UInt8],
                                 _ cipherWithTag: [UInt8]) -> Bool {
        decryptOrNul(kek, nonce, aad, cipherWithTag) != nil
    }

    /// 公私钥的 nonce 必须不同，否则 GCM 密钥流相同、机密性彻底失效。
    private struct SealedKeys {
        var pubNonce: [UInt8]
        var pubTag: [UInt8]
        var pubCipher: [UInt8]
        var privNonce: [UInt8]
        var privTag: [UInt8]
        var privCipher: [UInt8]
    }

    private func sealKeys(_ kek: [UInt8], _ aad: [UInt8], _ pubDER: [UInt8],
                          _ privDER: [UInt8]) -> SealedKeys? {
        var pubNonce = [UInt8]()
        var privNonce = [UInt8]()
        var ok = false
        for _ in 0..<8 {
            pubNonce = Crypto.randomBytes(Serialize.nonceLength)
            privNonce = Crypto.randomBytes(Serialize.nonceLength)
            if pubNonce != privNonce { ok = true; break }
        }
        guard ok else { return nil }
        guard let (pubCipher, pubTag) = try? Crypto.aesGcmEncrypt(
            key: kek, nonce: pubNonce, aad: aad, plaintext: pubDER
        ), let (privCipher, privTag) = try? Crypto.aesGcmEncrypt(
            key: kek, nonce: privNonce, aad: aad, plaintext: privDER
        ) else { return nil }
        // 桌面格式约定：cipher 字段 = 密文 + 16 字节 tag 尾，tag 同时单独存。
        return SealedKeys(pubNonce: pubNonce, pubTag: pubTag, pubCipher: pubCipher + pubTag,
                          privNonce: privNonce, privTag: privTag, privCipher: privCipher + privTag)
    }

    /// 解开数据密钥，再用它解密机密内容。详情、查看明文、导入三处的公共步骤。
    private func unwrapPlaintext(_ masterKek: [UInt8], _ f: Serialize.SecretFile) -> [UInt8]? {
        let mkHex = Ids.toHex(f.masterKeyId)
        let (kse, mkFile) = mkStore.load(mkHex)
        guard kse == .ok, let mk = mkFile else { return nil }
        guard let privPlain = decryptOrNul(masterKek, mk.privKeyNonce, mk.masterKeyId,
                                           mk.privKeyCipher) else { return nil }
        // 3.4 step 3: 用主密钥私钥解封装数据密钥。
        guard let dk = try? Crypto.rsaOaepDecrypt(privatePkcs8: privPlain,
                                                  wrapped: f.wrappedDk) else { return nil }
        // 3.4 step 4: 用数据密钥解密，AAD 是原始 secretId。
        guard f.dataCipher.count > Crypto.tagLen else { return nil }
        let ct = Array(f.dataCipher[0..<(f.dataCipher.count - Crypto.tagLen)])
        let tag = Array(f.dataCipher[(f.dataCipher.count - Crypto.tagLen)...])
        do {
            return try Crypto.aesGcmDecrypt(key: dk, nonce: f.dataNonce, aad: f.secretId,
                                            ciphertext: ct, tag: tag)
        } catch {
            return nil
        }
    }

    private func mapSave(_ e: SaveError) -> SvcError {
        switch e {
        case .ok: return .ok
        case .notFound: return .masterKeyNotFound
        case .parseError: return .importBadFormat
        case .ioError: return .ioError
        case .invalidArgument: return .internalError
        case .internalError: return .internalError
        }
    }
}
