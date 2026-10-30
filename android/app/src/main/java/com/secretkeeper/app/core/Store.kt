package com.secretkeeper.app.core

import java.io.File
import java.io.IOException

/**
 * 存储层：SQLite 索引库 + 数据文件读写。
 *
 * 与桌面端 src/common/core/store.cpp 保持同一职责边界：
 *   1. 索引库：保存 ID、名称、标题、路径等**元数据**（表结构见
 *      docs/03-data/数据格式与存储设计.md §6.3）；
 *   2. 数据文件：按 §6.1 目录布局读写字节流，字节 <-> 对象由 Serialize 完成。
 *
 * 本层不含任何密钥材料、密码、盐或明文机密信息。
 * 限额不在本层：2/15/150 是业务规则，由 Service 判定，本层只如实计数。
 *
 * SQLite 访问经由 [SqlDb] 薄接口：单元测试用 JDBC 适配器跑真 SQLite，
 * 运行时用 Android framework SQLite 适配器。SQL 使用两者共有的标准子集。
 */

/** 最小 SQLite 抽象：参数化执行与逐行读取。 */
interface SqlDb {
    /** 执行 DDL 或无参语句。 */
    fun exec(sql: String)

    /** 参数化更新（INSERT/UPDATE/DELETE），返回受影响行数。 */
    fun execute(sql: String, bind: List<Any?> = emptyList()): Int

    /** 参数化查询，逐行回调。 */
    fun query(sql: String, bind: List<Any?> = emptyList(), row: (SqlRow) -> Unit)

    fun close()
}

/** 查询行读取视图。列下标从 0 开始。 */
interface SqlRow {
    fun string(index: Int): String?

    fun long(index: Int): Long
}

/** 存储错误。不含限额类错误：限额由 Service 判定。 */
enum class StoreError {
    OK,
    NOT_FOUND,
    INVALID_ARGUMENT,
    BUSY,
    IO_ERROR,
    INTERNAL,
}

/** 对象级保存错误。 */
enum class SaveError {
    OK,
    INVALID_ARGUMENT, // ID 非法 / 字段缺失
    IO_ERROR,         // 目录不可写、磁盘满、文件被占用
    PARSE_ERROR,      // 读取时格式非法（详见 Serialize.ParseError）
    NOT_FOUND,        // 记录或文件不存在
    INTERNAL,         // 索引登记失败
}

/** 索引库行对象。时间戳为 Unix 毫秒。 */
data class MasterKeyRow(
    val masterKeyId: String, // 32 位小写十六进制
    val name: String,        // 可为空
    val filePath: String,    // 相对数据目录
    val mkAlg: Long = 1,     // 1 = RSA-2048
    val isDefault: Boolean = false,
    val createdAt: Long = 0,
    val updatedAt: Long = 0,
)

data class SecretRow(
    val secretId: String,
    val masterKeyId: String, // 允许悬空：主密钥可能已被删除
    val title: String,       // 可为空
    val filePath: String,
    val createdAt: Long = 0,
    val updatedAt: Long = 0,
)

/** 十六进制 ID 与字节 ID 互转。严格校验：奇数长度、非法字符一律拒绝。 */
object Ids {
    const val HEX_CHARS_PER_ID = Serialize.ID_LENGTH * 2 // 32

    fun fromHex(hex: String): ByteArray? {
        if (hex.length != HEX_CHARS_PER_ID) return null
        val out = ByteArray(Serialize.ID_LENGTH)
        for (i in 0 until HEX_CHARS_PER_ID) {
            val c = hex[i]
            val v = when (c) {
                in '0'..'9' -> c - '0'
                in 'a'..'f' -> c - 'a' + 10
                in 'A'..'F' -> c - 'A' + 10
                else -> return null
            }
            out[i / 2] = ((out[i / 2].toInt() shl 4) or v).toByte()
        }
        return out
    }

    fun toHex(id: ByteArray): String {
        val sb = StringBuilder(id.size * 2)
        for (b in id) {
            val v = b.toInt() and 0xFF
            sb.append("0123456789abcdef"[v ushr 4])
            sb.append("0123456789abcdef"[v and 0xF])
        }
        return sb.toString()
    }
}

/**
 * SQLite 索引库。表结构对应 docs/03-data §6.3：
 * 允许悬空引用（secrets.master_key_id 不建外键）；
 * is_default 的唯一性由 Service 层事务保证。
 */
class IndexDb(private val db: SqlDb) {
    companion object {
        private const val SCHEMA = """
            CREATE TABLE IF NOT EXISTS app_meta (
                key   TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS master_keys (
                master_key_id TEXT PRIMARY KEY,
                name          TEXT NOT NULL,
                file_path     TEXT NOT NULL,
                mk_alg        INTEGER NOT NULL,
                is_default    INTEGER NOT NULL,
                created_at    INTEGER NOT NULL,
                updated_at    INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS secrets (
                secret_id     TEXT PRIMARY KEY,
                master_key_id TEXT NOT NULL,
                title         TEXT NOT NULL DEFAULT '',
                file_path     TEXT NOT NULL,
                created_at    INTEGER NOT NULL,
                updated_at    INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_secrets_mk ON secrets(master_key_id);
        """
    }

    fun createSchema() {
        // SqlDb.exec 一次只保证执行一条语句，逐条拆分（与桌面 create_schema 一致）
        SCHEMA.trim().split(";").forEach { stmt ->
            val s = stmt.trim()
            if (s.isNotEmpty()) db.exec(s)
        }
    }

    /** upsert 语义：ID 已存在则更新（保留原有 created_at），不存在则插入。 */
    fun upsertMasterKey(row: MasterKeyRow): StoreError {
        val updated = db.execute(
            "UPDATE master_keys SET name = ?, file_path = ?, mk_alg = ?, " +
                "is_default = ?, updated_at = ? WHERE master_key_id = ?",
            listOf(row.name, row.filePath, row.mkAlg, if (row.isDefault) 1 else 0,
                row.updatedAt, row.masterKeyId),
        )
        if (updated == 0) {
            val inserted = db.execute(
                "INSERT INTO master_keys(master_key_id, name, file_path, mk_alg, " +
                    "is_default, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
                listOf(row.masterKeyId, row.name, row.filePath, row.mkAlg,
                    if (row.isDefault) 1 else 0, row.createdAt, row.updatedAt),
            )
            if (inserted != 1) return StoreError.INTERNAL
        }
        if (!row.isDefault) return StoreError.OK
        // 唯一默认主密钥：先把其他记录降级，再登记 app_meta（与桌面 store.cpp
        // 的同一事务语义对齐，避免留下两个 is_default=1 的记录）。
        db.exec("BEGIN IMMEDIATE")
        val cleared = db.execute(
            "UPDATE master_keys SET is_default = 0 " +
                "WHERE is_default = 1 AND master_key_id <> ?",
            listOf(row.masterKeyId),
        )
        val metaOk = setDefaultMasterKeyId(row.masterKeyId)
        if (cleared < 0 || metaOk != StoreError.OK) {
            db.exec("ROLLBACK")
            return StoreError.INTERNAL
        }
        db.exec("COMMIT")
        return StoreError.OK
    }

    private fun setDefaultMasterKeyId(masterKeyId: String): StoreError {
        val updated = db.execute(
            "UPDATE app_meta SET value = ? WHERE key = 'default_master_key_id'",
            listOf(masterKeyId),
        )
        if (updated > 0) return StoreError.OK
        val inserted = db.execute(
            "INSERT INTO app_meta(key, value) VALUES ('default_master_key_id', ?)",
            listOf(masterKeyId),
        )
        return if (inserted == 1) StoreError.OK else StoreError.INTERNAL
    }

    fun upsertSecret(row: SecretRow): StoreError {
        val updated = db.execute(
            "UPDATE secrets SET master_key_id = ?, title = ?, file_path = ?, " +
                "updated_at = ? WHERE secret_id = ?",
            listOf(row.masterKeyId, row.title, row.filePath, row.updatedAt, row.secretId),
        )
        if (updated > 0) return StoreError.OK
        val inserted = db.execute(
            "INSERT INTO secrets(secret_id, master_key_id, title, file_path, " +
                "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?)",
            listOf(row.secretId, row.masterKeyId, row.title, row.filePath,
                row.createdAt, row.updatedAt),
        )
        return if (inserted == 1) StoreError.OK else StoreError.INTERNAL
    }

    fun deleteMasterKey(masterKeyId: String): StoreError =
        if (db.execute("DELETE FROM master_keys WHERE master_key_id = ?",
                listOf(masterKeyId)) > 0) StoreError.OK else StoreError.NOT_FOUND

    fun deleteSecret(secretId: String): StoreError =
        if (db.execute("DELETE FROM secrets WHERE secret_id = ?",
                listOf(secretId)) > 0) StoreError.OK else StoreError.NOT_FOUND

    fun findMasterKey(masterKeyId: String): MasterKeyRow? {
        var found: MasterKeyRow? = null
        db.query(
            "SELECT master_key_id, name, file_path, mk_alg, is_default, " +
                "created_at, updated_at FROM master_keys WHERE master_key_id = ?",
            listOf(masterKeyId),
        ) { r ->
            found = MasterKeyRow(
                masterKeyId = r.string(0) ?: return@query,
                name = r.string(1) ?: "",
                filePath = r.string(2) ?: "",
                mkAlg = r.long(3),
                isDefault = r.long(4) != 0L,
                createdAt = r.long(5),
                updatedAt = r.long(6),
            )
        }
        return found
    }

    fun findSecret(secretId: String): SecretRow? {
        var found: SecretRow? = null
        db.query(
            "SELECT secret_id, master_key_id, title, file_path, created_at, " +
                "updated_at FROM secrets WHERE secret_id = ?",
            listOf(secretId),
        ) { r ->
            found = SecretRow(
                secretId = r.string(0) ?: return@query,
                masterKeyId = r.string(1) ?: "",
                title = r.string(2) ?: "",
                filePath = r.string(3) ?: "",
                createdAt = r.long(4),
                updatedAt = r.long(5),
            )
        }
        return found
    }

    fun listMasterKeys(): List<MasterKeyRow> {
        val out = mutableListOf<MasterKeyRow>()
        db.query(
            "SELECT master_key_id, name, file_path, mk_alg, is_default, " +
                "created_at, updated_at FROM master_keys ORDER BY created_at, master_key_id",
        ) { r ->
            out.add(
                MasterKeyRow(
                    masterKeyId = r.string(0) ?: "",
                    name = r.string(1) ?: "",
                    filePath = r.string(2) ?: "",
                    mkAlg = r.long(3),
                    isDefault = r.long(4) != 0L,
                    createdAt = r.long(5),
                    updatedAt = r.long(6),
                ),
            )
        }
        return out
    }

    fun listSecrets(): List<SecretRow> {
        val out = mutableListOf<SecretRow>()
        db.query(
            "SELECT secret_id, master_key_id, title, file_path, created_at, " +
                "updated_at FROM secrets ORDER BY created_at, secret_id",
        ) { r ->
            out.add(
                SecretRow(
                    secretId = r.string(0) ?: "",
                    masterKeyId = r.string(1) ?: "",
                    title = r.string(2) ?: "",
                    filePath = r.string(3) ?: "",
                    createdAt = r.long(4),
                    updatedAt = r.long(5),
                ),
            )
        }
        return out
    }

    fun listSecretsByMasterKey(masterKeyId: String): List<SecretRow> {
        val out = mutableListOf<SecretRow>()
        db.query(
            "SELECT secret_id, master_key_id, title, file_path, created_at, " +
                "updated_at FROM secrets WHERE master_key_id = ? ORDER BY created_at, secret_id",
            listOf(masterKeyId),
        ) { r ->
            out.add(
                SecretRow(
                    secretId = r.string(0) ?: "",
                    masterKeyId = r.string(1) ?: "",
                    title = r.string(2) ?: "",
                    filePath = r.string(3) ?: "",
                    createdAt = r.long(4),
                    updatedAt = r.long(5),
                ),
            )
        }
        return out
    }

    /** 默认主密钥。未设置时返回 null。 */
    fun defaultMasterKeyId(): String? {
        var found: String? = null
        db.query("SELECT value FROM app_meta WHERE key = 'default_master_key_id'") { r ->
            found = r.string(0)
        }
        return found
    }

    fun setDefaultMasterKey(masterKeyId: String): StoreError {
        if (findMasterKey(masterKeyId) == null) return StoreError.NOT_FOUND
        // 与桌面一致：走 upsert 的默认标记路径，避免两个真相源。
        val row = findMasterKey(masterKeyId)!!
        return upsertMasterKey(row.copy(isDefault = true))
    }

    /** 指向该主密钥的机密信息条数；用于删除前的影响面提示。 */
    fun countSecretsForMasterKey(masterKeyId: String): Int {
        var count = 0
        db.query(
            "SELECT COUNT(*) FROM secrets WHERE master_key_id = ?", listOf(masterKeyId),
        ) { r -> count = r.long(0).toInt() }
        return count
    }

    /** 纯计数。是否超限由 Service 判定。 */
    fun masterKeyCount(): Int {
        var count = 0
        db.query("SELECT COUNT(*) FROM master_keys") { r -> count = r.long(0).toInt() }
        return count
    }

    fun secretCount(): Int {
        var count = 0
        db.query("SELECT COUNT(*) FROM secrets") { r -> count = r.long(0).toInt() }
        return count
    }

    fun meta(key: String): String? {
        var found: String? = null
        db.query("SELECT value FROM app_meta WHERE key = ?", listOf(key)) { r ->
            found = r.string(0)
        }
        return found
    }

    fun setMeta(key: String, value: String): StoreError {
        val updated = db.execute(
            "UPDATE app_meta SET value = ? WHERE key = ?", listOf(value, key),
        )
        if (updated > 0) return StoreError.OK
        val inserted = db.execute(
            "INSERT INTO app_meta(key, value) VALUES (?, ?)", listOf(key, value),
        )
        return if (inserted == 1) StoreError.OK else StoreError.INTERNAL
    }

    fun close() {
        db.close()
    }
}

/**
 * 数据文件读写（裸字节）。目录布局对应 docs/03-data §6.1：
 *   <data_dir>/secret.db          索引库（由 IndexDb 管理）
 *   <data_dir>/keys/<id>.smk     主密钥数据文件
 *   <data_dir>/secrets/<id>.ssc  机密信息数据文件
 * 文件名完全由 ID 派生（纯 ASCII 十六进制），无随机性。
 */
class FileStore(val dataDir: File) {
    /** 建目录并做基本的可写性自检。已存在也算成功。 */
    fun ensureLayout(): Boolean {
        val keys = File(dataDir, "keys")
        val secrets = File(dataDir, "secrets")
        return (dataDir.isDirectory || dataDir.mkdirs()) &&
            (keys.isDirectory || keys.mkdirs()) &&
            (secrets.isDirectory || secrets.mkdirs()) &&
            dataDir.isDirectory && keys.isDirectory && secrets.isDirectory
    }

    fun masterKeyRelPath(masterKeyIdHex: String): String = "keys/$masterKeyIdHex.smk"

    fun secretRelPath(secretIdHex: String): String = "secrets/$secretIdHex.ssc"

    /**
     * 写入采用「先写临时文件再原子改名」，避免崩溃或断电留下半截文件
     * 被后续读取当成合法记录。
     */
    fun writeAtomic(relPath: String, bytes: ByteArray): Boolean {
        val target = File(dataDir, relPath)
        val parent = target.parentFile ?: return false
        if (!parent.isDirectory && !parent.mkdirs()) return false
        val tmp = File(parent, target.name + ".tmp")
        return try {
            tmp.writeBytes(bytes)
            if (target.exists() && !target.delete()) {
                tmp.delete()
                return false
            }
            // java.io.File.renameTo 是同一目录内的名字改写，等价于原子 rename
            tmp.renameTo(target)
        } catch (e: IOException) {
            tmp.delete()
            false
        }
    }

    fun read(relPath: String): ByteArray? = try {
        val f = File(dataDir, relPath)
        if (f.isFile) f.readBytes() else null
    } catch (e: IOException) {
        null
    }

    fun remove(relPath: String): Boolean {
        val f = File(dataDir, relPath)
        return if (!f.exists()) true else f.delete()
    }
}

/** 保存并登记索引。file_path 由本层按 ID 派生后写入索引行。 */
class MasterKeyStore(private val db: IndexDb, private val files: FileStore) {

    fun save(file: Serialize.MasterKeyFile, name: String, makeDefault: Boolean): SaveError {
        val idHex = Ids.toHex(file.masterKeyId)
        if (Ids.fromHex(idHex) == null || idHex.length != Ids.HEX_CHARS_PER_ID) {
            return SaveError.INVALID_ARGUMENT
        }
        val relPath = files.masterKeyRelPath(idHex)
        if (!files.writeAtomic(relPath, Serialize.serializeMasterKey(file))) {
            return SaveError.IO_ERROR
        }
        val now = System.currentTimeMillis()
        val err = db.upsertMasterKey(
            MasterKeyRow(
                masterKeyId = idHex,
                name = name,
                filePath = relPath,
                mkAlg = file.mkAlg,
                isDefault = makeDefault,
                createdAt = now,
                updatedAt = now,
            ),
        )
        if (err != StoreError.OK) return SaveError.INTERNAL
        if (makeDefault) {
            if (db.setDefaultMasterKey(idHex) != StoreError.OK) return SaveError.INTERNAL
        }
        return SaveError.OK
    }

    fun load(masterKeyIdHex: String): Pair<SaveError, Serialize.MasterKeyFile?> {
        if (masterKeyIdHex.length != Ids.HEX_CHARS_PER_ID || Ids.fromHex(masterKeyIdHex) == null) {
            return Pair(SaveError.INVALID_ARGUMENT, null)
        }
        val row = db.findMasterKey(masterKeyIdHex) ?: return Pair(SaveError.NOT_FOUND, null)
        val bytes = files.read(row.filePath) ?: return Pair(SaveError.IO_ERROR, null)
        val parsed = Serialize.parseMasterKey(bytes)
        return when (parsed) {
            is Serialize.Result.Ok -> Pair(SaveError.OK, parsed.value)
            is Serialize.Result.Err -> Pair(SaveError.PARSE_ERROR, null)
        }
    }
}

class SecretStore(private val db: IndexDb, private val files: FileStore) {

    fun save(file: Serialize.SecretFile): SaveError {
        val idHex = Ids.toHex(file.secretId)
        if (Ids.fromHex(idHex) == null || idHex.length != Ids.HEX_CHARS_PER_ID) {
            return SaveError.INVALID_ARGUMENT
        }
        val relPath = files.secretRelPath(idHex)
        if (!files.writeAtomic(relPath, Serialize.serializeSecret(file))) {
            return SaveError.IO_ERROR
        }
        val now = System.currentTimeMillis()
        val err = db.upsertSecret(
            SecretRow(
                secretId = idHex,
                masterKeyId = Ids.toHex(file.masterKeyId),
                title = file.title,
                filePath = relPath,
                createdAt = now,
                updatedAt = now,
            ),
        )
        return if (err == StoreError.OK) SaveError.OK else SaveError.INTERNAL
    }

    fun load(secretIdHex: String): Pair<SaveError, Serialize.SecretFile?> {
        if (secretIdHex.length != Ids.HEX_CHARS_PER_ID || Ids.fromHex(secretIdHex) == null) {
            return Pair(SaveError.INVALID_ARGUMENT, null)
        }
        val row = db.findSecret(secretIdHex) ?: return Pair(SaveError.NOT_FOUND, null)
        val bytes = files.read(row.filePath) ?: return Pair(SaveError.IO_ERROR, null)
        val parsed = Serialize.parseSecret(bytes)
        return when (parsed) {
            is Serialize.Result.Ok -> Pair(SaveError.OK, parsed.value)
            is Serialize.Result.Err -> Pair(SaveError.PARSE_ERROR, null)
        }
    }
}
