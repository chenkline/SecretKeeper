import Foundation
import CSQLite

/// 存储层：SQLite 索引库 + 数据文件读写。
///
/// 与桌面端 src/common/core/store.cpp 保持同一职责边界（移植自
/// android/.../Store.kt，已通过 StoreTest）：
///   1. 索引库：保存 ID、名称、标题、路径等**元数据**（docs/03-data §6.3）；
///   2. 数据文件：按 §6.1 目录布局读写字节流。
/// 本层不含任何密钥材料；限额由 Service 判定，本层只如实计数。
/// SQLite 走系统库（iOS/macOS 系统 SDK；Linux 装 libsqlite3-dev）。

/// 存储错误。不含限额类错误：限额由 Service 判定。
public enum StoreError: Equatable {
    case ok
    case notFound
    case invalidArgument
    case busy
    case ioError
    case internalError
}

/// 对象级保存错误。
public enum SaveError: Equatable {
    case ok
    case invalidArgument // ID 非法 / 字段缺失
    case ioError         // 目录不可写、磁盘满
    case parseError      // 读取时格式非法
    case notFound        // 记录或文件不存在
    case internalError   // 索引登记失败
}

/// 索引库行对象。时间戳为 Unix 毫秒。
public struct MasterKeyRow: Equatable {
    public var masterKeyId: String // 32 位小写十六进制
    public var name: String        // 可为空
    public var filePath: String    // 相对数据目录
    public var mkAlg: Int64        // 1 = RSA-2048
    public var isDefault: Bool
    public var createdAt: Int64
    public var updatedAt: Int64

    public init(masterKeyId: String, name: String, filePath: String,
                mkAlg: Int64 = 1, isDefault: Bool = false,
                createdAt: Int64 = 0, updatedAt: Int64 = 0) {
        self.masterKeyId = masterKeyId
        self.name = name
        self.filePath = filePath
        self.mkAlg = mkAlg
        self.isDefault = isDefault
        self.createdAt = createdAt
        self.updatedAt = updatedAt
    }
}

public struct SecretRow: Equatable {
    public var secretId: String
    public var masterKeyId: String // 允许悬空：主密钥可能已被删除
    public var title: String       // 可为空
    public var filePath: String
    public var createdAt: Int64
    public var updatedAt: Int64

    public init(secretId: String, masterKeyId: String, title: String,
                filePath: String, createdAt: Int64 = 0, updatedAt: Int64 = 0) {
        self.secretId = secretId
        self.masterKeyId = masterKeyId
        self.title = title
        self.filePath = filePath
        self.createdAt = createdAt
        self.updatedAt = updatedAt
    }
}

/// 十六进制 ID 与字节 ID 互转。严格校验：奇数长度、非法字符一律拒绝。
public enum Ids {
    public static let hexCharsPerId = Serialize.idLength * 2 // 32

    public static func fromHex(_ hex: String) -> [UInt8]? {
        let chars = Array(hex.utf8)
        guard chars.count == hexCharsPerId else { return nil }
        var out = [UInt8](repeating: 0, count: Serialize.idLength)
        for i in 0..<hexCharsPerId {
            let c = chars[i]
            let v: UInt8
            switch c {
            case 0x30...0x39: v = c - 0x30
            case 0x61...0x66: v = c - 0x61 + 10
            case 0x41...0x46: v = c - 0x41 + 10
            default: return nil
            }
            out[i / 2] = (out[i / 2] << 4) | v
        }
        return out
    }

    public static func toHex(_ id: [UInt8]) -> String {
        let digits = Array("0123456789abcdef".utf8)
        var out = [UInt8]()
        out.reserveCapacity(id.count * 2)
        for b in id {
            out.append(digits[Int(b >> 4)])
            out.append(digits[Int(b & 0xF)])
        }
        return String(decoding: out, as: UTF8.self)
    }
}

/// sqlite3_bind_text 的析构器：SQLITE_TRANSIENT（让 SQLite 自拷贝缓冲）。
private let SQLITE_TRANSIENT = unsafeBitCast(
    OpaquePointer(bitPattern: Int(-1)), to: sqlite3_destructor_type.self)

/// SQLite 索引库。表结构对应 docs/03-data §6.3：
/// 允许悬空引用（secrets.master_key_id 不建外键）；
/// is_default 的唯一性由 upsert 内同一事务降级其余行保证。
public final class IndexDb {
    private var db: OpaquePointer?

    /// 打开（或创建）数据库文件。
    public init(path: String) throws {
        var handle: OpaquePointer?
        guard sqlite3_open_v2(path, &handle,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nil) == SQLITE_OK,
              handle != nil else {
            if let h = handle { sqlite3_close(h) }
            throw StoreFailure.cannotOpen
        }
        db = handle
    }

    deinit {
        if let d = db { sqlite3_close(d) }
    }

    public enum StoreFailure: Error { case cannotOpen }

    public func createSchema() {
        // 与桌面 create_schema 一致的逐条语句
        let statements = [
            "CREATE TABLE IF NOT EXISTS app_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
            "CREATE TABLE IF NOT EXISTS master_keys ("
                + "master_key_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
                + "file_path TEXT NOT NULL, mk_alg INTEGER NOT NULL, "
                + "is_default INTEGER NOT NULL, created_at INTEGER NOT NULL, "
                + "updated_at INTEGER NOT NULL)",
            "CREATE TABLE IF NOT EXISTS secrets ("
                + "secret_id TEXT PRIMARY KEY, master_key_id TEXT NOT NULL, "
                + "title TEXT NOT NULL DEFAULT '', file_path TEXT NOT NULL, "
                + "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)",
            "CREATE INDEX IF NOT EXISTS idx_secrets_mk ON secrets(master_key_id)"
        ]
        for sql in statements { exec(sql) }
    }

    private func exec(_ sql: String) {
        sqlite3_exec(db, sql, nil, nil, nil)
    }

    // ---- 底层绑定/步进 ----

    private func prepare(_ sql: String, _ bind: [Any]) -> OpaquePointer? {
        var st: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &st, nil) == SQLITE_OK else { return nil }
        for (i, v) in bind.enumerated() {
            let idx = Int32(i + 1)
            switch v {
            case let s as String: sqlite3_bind_text(st, idx, s, -1, SQLITE_TRANSIENT)
            case let n as Int64: sqlite3_bind_int64(st, idx, n)
            case let n as Int: sqlite3_bind_int64(st, idx, Int64(n))
            case let b as Bool: sqlite3_bind_int64(st, idx, b ? 1 : 0)
            case let n as Int32: sqlite3_bind_int64(st, idx, Int64(n))
            default: sqlite3_bind_null(st, idx)
            }
        }
        return st
    }

    @discardableResult
    private func run(_ sql: String, _ bind: [Any] = []) -> Int {
        guard let st = prepare(sql, bind) else { return 0 }
        defer { sqlite3_finalize(st) }
        let rc = sqlite3_step(st)
        return rc == SQLITE_DONE || rc == SQLITE_ROW ? Int(sqlite3_changes(db)) : 0
    }

    private func query(_ sql: String, _ bind: [Any] = [],
                       _ row: (OpaquePointer) -> Void) {
        guard let st = prepare(sql, bind) else { return }
        defer { sqlite3_finalize(st) }
        while sqlite3_step(st) == SQLITE_ROW { row(st) }
    }

    private func text(_ st: OpaquePointer, _ i: Int32) -> String {
        guard let c = sqlite3_column_text(st, i) else { return "" }
        return String(cString: c)
    }

    // ---- upsert ----

    /// upsert 语义：ID 已存在则更新（保留原有 created_at），不存在则插入。
    @discardableResult
    public func upsertMasterKey(_ row: MasterKeyRow) -> StoreError {
        let updated = run(
            "UPDATE master_keys SET name = ?, file_path = ?, mk_alg = ?, "
                + "is_default = ?, updated_at = ? WHERE master_key_id = ?",
            [row.name, row.filePath, row.mkAlg, row.isDefault, row.updatedAt, row.masterKeyId]
        )
        if updated == 0 {
            let inserted = run(
                "INSERT INTO master_keys(master_key_id, name, file_path, mk_alg, "
                    + "is_default, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
                [row.masterKeyId, row.name, row.filePath, row.mkAlg, row.isDefault,
                 row.createdAt, row.updatedAt]
            )
            if inserted == 0 { return .internalError }
        }
        guard row.isDefault else { return .ok }
        // 唯一默认主密钥：先把其他记录降级，再登记 app_meta（与桌面同事务语义）。
        exec("BEGIN IMMEDIATE")
        let cleared = run(
            "UPDATE master_keys SET is_default = 0 WHERE is_default = 1 AND master_key_id <> ?",
            [row.masterKeyId]
        )
        let metaOk = setDefaultMasterKeyId(row.masterKeyId)
        if cleared < 0 || metaOk != .ok {
            exec("ROLLBACK")
            return .internalError
        }
        exec("COMMIT")
        return .ok
    }

    @discardableResult
    public func upsertSecret(_ row: SecretRow) -> StoreError {
        let updated = run(
            "UPDATE secrets SET master_key_id = ?, title = ?, file_path = ?, "
                + "updated_at = ? WHERE secret_id = ?",
            [row.masterKeyId, row.title, row.filePath, row.updatedAt, row.secretId]
        )
        if updated == 0 {
            let inserted = run(
                "INSERT INTO secrets(secret_id, master_key_id, title, file_path, "
                    + "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?)",
                [row.secretId, row.masterKeyId, row.title, row.filePath,
                 row.createdAt, row.updatedAt]
            )
            return inserted == 1 ? .ok : .internalError
        }
        return .ok
    }

    @discardableResult
    public func deleteMasterKey(_ masterKeyId: String) -> StoreError {
        run("DELETE FROM master_keys WHERE master_key_id = ?", [masterKeyId]) > 0
            ? .ok : .notFound
    }

    @discardableResult
    public func deleteSecret(_ secretId: String) -> StoreError {
        run("DELETE FROM secrets WHERE secret_id = ?", [secretId]) > 0 ? .ok : .notFound
    }

    public func findMasterKey(_ masterKeyId: String) -> MasterKeyRow? {
        var found: MasterKeyRow?
        query("SELECT master_key_id, name, file_path, mk_alg, is_default, "
            + "created_at, updated_at FROM master_keys WHERE master_key_id = ?",
            [masterKeyId]) { st in
            found = MasterKeyRow(
                masterKeyId: self.text(st, 0), name: self.text(st, 1),
                filePath: self.text(st, 2), mkAlg: sqlite3_column_int64(st, 3),
                isDefault: sqlite3_column_int64(st, 4) != 0,
                createdAt: sqlite3_column_int64(st, 5), updatedAt: sqlite3_column_int64(st, 6)
            )
        }
        return found
    }

    public func findSecret(_ secretId: String) -> SecretRow? {
        var found: SecretRow?
        query("SELECT secret_id, master_key_id, title, file_path, created_at, "
            + "updated_at FROM secrets WHERE secret_id = ?", [secretId]) { st in
            found = SecretRow(
                secretId: self.text(st, 0), masterKeyId: self.text(st, 1),
                title: self.text(st, 2), filePath: self.text(st, 3),
                createdAt: sqlite3_column_int64(st, 4), updatedAt: sqlite3_column_int64(st, 5)
            )
        }
        return found
    }

    public func listMasterKeys() -> [MasterKeyRow] {
        var out = [MasterKeyRow]()
        query("SELECT master_key_id, name, file_path, mk_alg, is_default, "
            + "created_at, updated_at FROM master_keys ORDER BY created_at, master_key_id") { st in
            out.append(MasterKeyRow(
                masterKeyId: self.text(st, 0), name: self.text(st, 1),
                filePath: self.text(st, 2), mkAlg: sqlite3_column_int64(st, 3),
                isDefault: sqlite3_column_int64(st, 4) != 0,
                createdAt: sqlite3_column_int64(st, 5), updatedAt: sqlite3_column_int64(st, 6)
            ))
        }
        return out
    }

    public func listSecrets() -> [SecretRow] {
        var out = [SecretRow]()
        query("SELECT secret_id, master_key_id, title, file_path, created_at, "
            + "updated_at FROM secrets ORDER BY created_at, secret_id") { st in
            out.append(SecretRow(
                secretId: self.text(st, 0), masterKeyId: self.text(st, 1),
                title: self.text(st, 2), filePath: self.text(st, 3),
                createdAt: sqlite3_column_int64(st, 4), updatedAt: sqlite3_column_int64(st, 5)
            ))
        }
        return out
    }

    public func listSecretsByMasterKey(_ masterKeyId: String) -> [SecretRow] {
        var out = [SecretRow]()
        query("SELECT secret_id, master_key_id, title, file_path, created_at, "
            + "updated_at FROM secrets WHERE master_key_id = ? ORDER BY created_at, secret_id",
            [masterKeyId]) { st in
            out.append(SecretRow(
                secretId: self.text(st, 0), masterKeyId: self.text(st, 1),
                title: self.text(st, 2), filePath: self.text(st, 3),
                createdAt: sqlite3_column_int64(st, 4), updatedAt: sqlite3_column_int64(st, 5)
            ))
        }
        return out
    }

    /// 默认主密钥。未设置时返回 nil。
    public func defaultMasterKeyId() -> String? {
        var found: String?
        query("SELECT value FROM app_meta WHERE key = 'default_master_key_id'") { st in
            found = self.text(st, 0)
        }
        return found
    }

    @discardableResult
    public func setDefaultMasterKey(_ masterKeyId: String) -> StoreError {
        guard findMasterKey(masterKeyId) != nil else { return .notFound }
        // 与桌面一致：走 upsert 的默认标记路径，避免两个真相源。
        guard let row = findMasterKey(masterKeyId) else { return .notFound }
        return upsertMasterKey(MasterKeyRow(
            masterKeyId: row.masterKeyId, name: row.name, filePath: row.filePath,
            mkAlg: row.mkAlg, isDefault: true, createdAt: row.createdAt,
            updatedAt: row.updatedAt
        ))
    }

    private func setDefaultMasterKeyId(_ masterKeyId: String) -> StoreError {
        let updated = run(
            "UPDATE app_meta SET value = ? WHERE key = 'default_master_key_id'",
            [masterKeyId]
        )
        if updated > 0 { return .ok }
        let inserted = run(
            "INSERT INTO app_meta(key, value) VALUES ('default_master_key_id', ?)",
            [masterKeyId]
        )
        return inserted == 1 ? .ok : .internalError
    }

    /// 指向该主密钥的机密信息条数；用于删除前的影响面提示。
    public func countSecretsForMasterKey(_ masterKeyId: String) -> Int {
        var count = 0
        query("SELECT COUNT(*) FROM secrets WHERE master_key_id = ?", [masterKeyId]) { st in
            count = Int(sqlite3_column_int64(st, 0))
        }
        return count
    }

    public func masterKeyCount() -> Int {
        var count = 0
        query("SELECT COUNT(*) FROM master_keys") { st in count = Int(sqlite3_column_int64(st, 0)) }
        return count
    }

    public func secretCount() -> Int {
        var count = 0
        query("SELECT COUNT(*) FROM secrets") { st in count = Int(sqlite3_column_int64(st, 0)) }
        return count
    }

    public func meta(_ key: String) -> String? {
        var found: String?
        query("SELECT value FROM app_meta WHERE key = ?", [key]) { st in
            found = self.text(st, 0)
        }
        return found
    }

    @discardableResult
    public func setMeta(_ key: String, _ value: String) -> StoreError {
        let updated = run("UPDATE app_meta SET value = ? WHERE key = ?", [value, key])
        if updated > 0 { return .ok }
        let inserted = run("INSERT INTO app_meta(key, value) VALUES (?, ?)", [key, value])
        return inserted == 1 ? .ok : .internalError
    }
}

/// 数据文件读写。目录布局对应 docs/03-data §6.1：
///   <data_dir>/secret.db、<data_dir>/keys/<id>.smk、<data_dir>/secrets/<id>.ssc
/// 文件名完全由 ID 派生（纯 ASCII 十六进制），无随机性。
public final class FileStore {
    public let dataDir: String

    public init(dataDir: String) { self.dataDir = dataDir }

    /// 建目录并做基本的可写性自检。已存在也算成功。
    @discardableResult
    public func ensureLayout() -> Bool {
        let fm = FileManager.default
        for sub in ["", "keys", "secrets"] {
            let dir = (dataDir as NSString).appendingPathComponent(sub)
            if !fm.fileExists(atPath: dir) {
                do { try fm.createDirectory(atPath: dir, withIntermediateDirectories: true) }
                catch { return false }
            }
        }
        return true
    }

    public func masterKeyRelPath(_ masterKeyIdHex: String) -> String {
        "keys/\(masterKeyIdHex).smk"
    }

    public func secretRelPath(_ secretIdHex: String) -> String {
        "secrets/\(secretIdHex).ssc"
    }

    private func absolute(_ relPath: String) -> String {
        (dataDir as NSString).appendingPathComponent(relPath)
    }

    /// 写入采用「先写临时文件再原子改名」，避免崩溃留下半截文件。
    public func writeAtomic(_ relPath: String, _ bytes: [UInt8]) -> Bool {
        let target = absolute(relPath)
        let dir = (target as NSString).deletingLastPathComponent
        if !FileManager.default.fileExists(atPath: dir) {
            do { try FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true) }
            catch { return false }
        }
        let tmp = target + ".tmp"
        let data = Data(bytes)
        guard fmWrite(tmp, data) else { return false }
        if FileManager.default.fileExists(atPath: target) {
            do { try FileManager.default.removeItem(atPath: target) }
            catch { try? FileManager.default.removeItem(atPath: tmp); return false }
        }
        return fmRename(tmp, target)
    }

    public func read(_ relPath: String) -> [UInt8]? {
        guard let data = FileManager.default.contents(atPath: absolute(relPath)) else {
            return nil
        }
        return [UInt8](data)
    }

    @discardableResult
    public func remove(_ relPath: String) -> Bool {
        let p = absolute(relPath)
        guard FileManager.default.fileExists(atPath: p) else { return true }
        return (try? FileManager.default.removeItem(atPath: p)) != nil
    }
}

private func fmWrite(_ path: String, _ data: Data) -> Bool {
    FileManager.default.createFile(atPath: path, contents: data)
}

private func fmRename(_ from: String, _ to: String) -> Bool {
    (try? FileManager.default.moveItem(atPath: from, toPath: to)) != nil
}

/// 保存并登记索引。file_path 由本层按 ID 派生后写入索引行。
/// 先落文件再登记索引（索引指向的文件必须已存在），登记失败撤文件不留孤儿。
public final class MasterKeyStore {
    let db: IndexDb
    let files: FileStore

    public init(db: IndexDb, files: FileStore) {
        self.db = db
        self.files = files
    }

    public func save(_ file: Serialize.MasterKeyFile, _ name: String,
                     makeDefault: Bool) -> SaveError {
        let idHex = Ids.toHex(file.masterKeyId)
        guard Ids.fromHex(idHex) != nil, idHex.count == Ids.hexCharsPerId else {
            return .invalidArgument
        }
        let rel = files.masterKeyRelPath(idHex)
        guard files.writeAtomic(rel, Serialize.serializeMasterKey(file)) else {
            return .ioError
        }
        let now = Int64(Date().timeIntervalSince1970 * 1000)
        let err = db.upsertMasterKey(MasterKeyRow(
            masterKeyId: idHex, name: name, filePath: rel, mkAlg: Int64(file.mkAlg),
            isDefault: makeDefault, createdAt: now, updatedAt: now
        ))
        if err != .ok {
            files.remove(rel) // 登记失败撤掉刚写的文件，不留孤儿
            return .internalError
        }
        return .ok
    }

    public func load(_ masterKeyIdHex: String) -> (SaveError, Serialize.MasterKeyFile?) {
        guard masterKeyIdHex.count == Ids.hexCharsPerId,
              Ids.fromHex(masterKeyIdHex) != nil else {
            return (.invalidArgument, nil)
        }
        guard let row = db.findMasterKey(masterKeyIdHex) else { return (.notFound, nil) }
        guard let bytes = files.read(row.filePath) else { return (.ioError, nil) }
        switch Serialize.parseMasterKey(bytes) {
        case .ok(let f): return (.ok, f)
        case .err: return (.parseError, nil)
        }
    }
}

public final class SecretStore {
    let db: IndexDb
    let files: FileStore

    public init(db: IndexDb, files: FileStore) {
        self.db = db
        self.files = files
    }

    public func save(_ file: Serialize.SecretFile) -> SaveError {
        let idHex = Ids.toHex(file.secretId)
        guard Ids.fromHex(idHex) != nil, idHex.count == Ids.hexCharsPerId else {
            return .invalidArgument
        }
        let rel = files.secretRelPath(idHex)
        guard files.writeAtomic(rel, Serialize.serializeSecret(file)) else {
            return .ioError
        }
        let now = Int64(Date().timeIntervalSince1970 * 1000)
        let err = db.upsertSecret(SecretRow(
            secretId: idHex, masterKeyId: Ids.toHex(file.masterKeyId),
            title: file.title, filePath: rel, createdAt: now, updatedAt: now
        ))
        if err != .ok {
            files.remove(rel)
            return .internalError
        }
        return .ok
    }

    public func load(_ secretIdHex: String) -> (SaveError, Serialize.SecretFile?) {
        guard secretIdHex.count == Ids.hexCharsPerId,
              Ids.fromHex(secretIdHex) != nil else {
            return (.invalidArgument, nil)
        }
        guard let row = db.findSecret(secretIdHex) else { return (.notFound, nil) }
        guard let bytes = files.read(row.filePath) else { return (.ioError, nil) }
        switch Serialize.parseSecret(bytes) {
        case .ok(let f): return (.ok, f)
        case .err: return (.parseError, nil)
        }
    }
}
