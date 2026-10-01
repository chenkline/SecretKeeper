#include "index_db.h"

#include <sqlite3.h>

#include <windows.h>

#include <utility>

namespace secretkeeper::store {
namespace {

// FILETIME 是 100ns 单位且起点为 1601-01-01，与 Unix 起点相差
// 11644473600 秒。直接换算，避免为拿时间戳再引入一个依赖。
std::int64_t now_millis() {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  const std::uint64_t ticks = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) |
                              ft.dwLowDateTime;
  constexpr std::uint64_t kTicksToUnixEpoch = 116444736000000000ULL;
  return static_cast<std::int64_t>((ticks - kTicksToUnixEpoch) / 10000);
}

// ID 一律是本模块生成的小写 32 位十六进制。拒绝大写与非十六进制输入，
// 可防止大小写混写绕过主键唯一性。
bool is_hex32(std::string_view s) {
  if (s.size() != 32) return false;
  for (char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

StoreError bind_text(sqlite3_stmt* st, int idx, std::string_view v) {
  // SQLITE_TRANSIENT 让 SQLite 自行复制一份，调用方的临时字符串随即失效也安全。
  return sqlite3_bind_text(st, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT) ==
                 SQLITE_OK
             ? StoreError::kOk
             : StoreError::kInternal;
}

StoreError bind_int64(sqlite3_stmt* st, int idx, std::int64_t v) {
  return sqlite3_bind_int64(st, idx, v) == SQLITE_OK ? StoreError::kOk : StoreError::kInternal;
}

std::string column_text(sqlite3_stmt* st, int idx) {
  const unsigned char* p = sqlite3_column_text(st, idx);
  if (p == nullptr) return std::string();
  return std::string(reinterpret_cast<const char*>(p),
                     static_cast<std::size_t>(sqlite3_column_bytes(st, idx)));
}

MasterKeyRow read_master_key_row(sqlite3_stmt* st) {
  MasterKeyRow r;
  r.master_key_id = column_text(st, 0);
  r.name = column_text(st, 1);
  r.file_path = column_text(st, 2);
  r.mk_alg = sqlite3_column_int64(st, 3);
  r.is_default = sqlite3_column_int(st, 4) != 0;
  r.created_at = sqlite3_column_int64(st, 5);
  r.updated_at = sqlite3_column_int64(st, 6);
  return r;
}

SecretRow read_secret_row(sqlite3_stmt* st) {
  SecretRow r;
  r.secret_id = column_text(st, 0);
  r.master_key_id = column_text(st, 1);
  r.title = column_text(st, 2);
  r.file_path = column_text(st, 3);
  r.created_at = sqlite3_column_int64(st, 4);
  r.updated_at = sqlite3_column_int64(st, 5);
  return r;
}

constexpr const char* kMasterKeyColumns =
    "master_key_id, name, file_path, mk_alg, is_default, created_at, updated_at";
constexpr const char* kSecretColumns =
    "secret_id, master_key_id, title, file_path, created_at, updated_at";

}  // namespace

const char* to_string(StoreError e) {
  switch (e) {
    case StoreError::kOk: return "ok";
    case StoreError::kNotFound: return "not_found";
    case StoreError::kQuotaExceededMasterKeys: return "quota_master_keys";
    case StoreError::kQuotaExceededSecrets: return "quota_secrets";
    case StoreError::kInvalidArgument: return "invalid_argument";
    case StoreError::kBusy: return "busy";
    case StoreError::kIoError: return "io_error";
    case StoreError::kInternal: return "internal";
  }
  return "unknown";
}

IndexDb::IndexDb(IndexDb&& o) noexcept : handle_(o.handle_), last_error_(o.last_error_) {
  o.handle_ = nullptr;
}

IndexDb& IndexDb::operator=(IndexDb&& o) noexcept {
  if (this != &o) {
    close();
    handle_ = o.handle_;
    last_error_ = o.last_error_;
    o.handle_ = nullptr;
  }
  return *this;
}

IndexDb::~IndexDb() { close(); }

void IndexDb::close() {
  if (handle_ != nullptr) {
    sqlite3_close(static_cast<sqlite3*>(handle_));
    handle_ = nullptr;
  }
}

StoreError IndexDb::exec(const char* sql) const {
  if (handle_ == nullptr) return StoreError::kInternal;
  char* err = nullptr;
  const int rc = sqlite3_exec(static_cast<sqlite3*>(handle_), sql, nullptr, nullptr, &err);
  if (err != nullptr) {
    last_error_ = sqlite3_mprintf("%s", err);
    sqlite3_free(err);
  }
  if (rc == SQLITE_OK) return StoreError::kOk;
  if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) return StoreError::kBusy;
  if (rc == SQLITE_READONLY || rc == SQLITE_CANTOPEN || rc == SQLITE_IOERR) {
    return StoreError::kIoError;
  }
  return StoreError::kInternal;
}

StoreError IndexDb::create_schema() const {
  // secrets.master_key_id 刻意不建外键：需求要求删除主密钥时保留其机密信息，
  // 因此悬空引用是合法状态，UI 靠主密钥 ID 列末尾的黄色问号标识。
  return exec(
      "CREATE TABLE IF NOT EXISTS app_meta ("
      "  key   TEXT PRIMARY KEY,"
      "  value TEXT NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS master_keys ("
      "  master_key_id TEXT PRIMARY KEY,"
      "  name          TEXT NOT NULL,"
      "  file_path     TEXT NOT NULL,"
      "  mk_alg        INTEGER NOT NULL,"
      "  is_default    INTEGER NOT NULL,"
      "  created_at    INTEGER NOT NULL,"
      "  updated_at    INTEGER NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS secrets ("
      "  secret_id     TEXT PRIMARY KEY,"
      "  master_key_id TEXT NOT NULL,"
      "  title         TEXT NOT NULL DEFAULT '',"
      "  file_path     TEXT NOT NULL,"
      "  created_at    INTEGER NOT NULL,"
      "  updated_at    INTEGER NOT NULL"
      ");"
      "CREATE INDEX IF NOT EXISTS idx_secrets_mk ON secrets(master_key_id);");
}

StoreError IndexDb::open(const std::string& db_path) {
  close();
  if (db_path.empty()) return StoreError::kInvalidArgument;

  sqlite3* db = nullptr;
  const int rc = sqlite3_open_v2(db_path.c_str(), &db,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                 nullptr);
  if (rc != SQLITE_OK) {
    if (db != nullptr) {
      last_error_ = sqlite3_errmsg(db);
      sqlite3_close(db);
    } else {
      last_error_ = "sqlite3_open_v2 failed";
    }
    return rc == SQLITE_CANTOPEN ? StoreError::kIoError : StoreError::kInternal;
  }
  handle_ = db;

  // WAL 让读不阻塞写。索引库丢失只影响元数据顺序，不会导致密钥或机密丢失，
  // 因为数据文件（.smk/.ssc）彼此独立，因此 synchronous=NORMAL 足够。
  {
    const StoreError e = exec("PRAGMA journal_mode=WAL;");
    if (e != StoreError::kOk) return e;
  }
  {
    const StoreError e = exec("PRAGMA synchronous=NORMAL;");
    if (e != StoreError::kOk) return e;
  }
  {
    const StoreError e = exec("PRAGMA foreign_keys=OFF;");
    if (e != StoreError::kOk) return e;
  }
  return create_schema();
}

StoreError IndexDb::upsert_master_key(const MasterKeyRow& row) {
  if (handle_ == nullptr) return StoreError::kInternal;
  if (!is_hex32(row.master_key_id) || row.file_path.empty()) return StoreError::kInvalidArgument;

  // 限额只在新增时生效；更新既有记录不得触发。
  const bool is_new = !find_master_key(row.master_key_id).has_value();
  if (is_new && master_key_count() >= kMaxMasterKeys) {
    return StoreError::kQuotaExceededMasterKeys;
  }

  if (!is_new && row.created_at == 0) {
    const std::optional<MasterKeyRow> old = find_master_key(row.master_key_id);
    if (old) {
      return upsert_master_key([&] {
               MasterKeyRow merged = row;
               merged.created_at = old->created_at;
               return merged;
             }());
    }
  }

  sqlite3_stmt* st = nullptr;
  // created_at 只在插入时取当前时间，冲突更新不覆盖，保持首次创建时间。
  const std::string sql = std::string(
                              "INSERT INTO master_keys (") +
                          kMasterKeyColumns +
                          ") VALUES(?,?,?,?,?,COALESCE(NULLIF(?,0),?),COALESCE(NULLIF(?,0),?)) "
                          "ON CONFLICT(master_key_id) DO UPDATE SET "
                          "name=excluded.name, file_path=excluded.file_path, "
                          "mk_alg=excluded.mk_alg, is_default=excluded.is_default, "
                          "updated_at=excluded.updated_at";
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(handle_), sql.c_str(), -1, &st, nullptr) !=
      SQLITE_OK) {
    last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
    return StoreError::kInternal;
  }

  const std::int64_t now = now_millis();
  StoreError e = bind_text(st, 1, row.master_key_id);
  if (e == StoreError::kOk) e = bind_text(st, 2, row.name);
  if (e == StoreError::kOk) e = bind_text(st, 3, row.file_path);
  if (e == StoreError::kOk) e = bind_int64(st, 4, row.mk_alg);
  if (e == StoreError::kOk) {
    e = sqlite3_bind_int(st, 5, row.is_default ? 1 : 0) == SQLITE_OK ? StoreError::kOk
                                                                    : StoreError::kInternal;
  }
  if (e == StoreError::kOk) e = bind_int64(st, 6, row.created_at);
  if (e == StoreError::kOk) e = bind_int64(st, 7, now);
  if (e == StoreError::kOk) e = bind_int64(st, 8, row.updated_at);
  if (e == StoreError::kOk) e = bind_int64(st, 9, now);
  if (e != StoreError::kOk) {
    sqlite3_finalize(st);
    return e;
  }

  if (sqlite3_step(st) != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
    sqlite3_finalize(st);
    return StoreError::kInternal;
  }
  sqlite3_finalize(st);

  if (!row.is_default) return StoreError::kOk;
  // 唯一默认主密钥：先把其他记录降级，再登记 app_meta。两步必须在同一
  // 事务里，否则中途崩溃会留下两个 is_default=1 的记录。
  if (exec("BEGIN IMMEDIATE;") != StoreError::kOk) return StoreError::kBusy;
  {
    sqlite3_stmt* clear = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(handle_),
                           "UPDATE master_keys SET is_default=0 "
                           "WHERE is_default=1 AND master_key_id<>?",
                           -1, &clear, nullptr) != SQLITE_OK) {
      last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
      exec("ROLLBACK;");
      return StoreError::kInternal;
    }
    bind_text(clear, 1, row.master_key_id);
    if (sqlite3_step(clear) != SQLITE_DONE) {
      last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
      sqlite3_finalize(clear);
      exec("ROLLBACK;");
      return StoreError::kInternal;
    }
    sqlite3_finalize(clear);
  }
  const StoreError meta_rc = set_meta("default_master_key_id", row.master_key_id);
  if (meta_rc != StoreError::kOk) {
    exec("ROLLBACK;");
    return meta_rc;
  }
  if (exec("COMMIT;") != StoreError::kOk) return StoreError::kBusy;
  return StoreError::kOk;
}

StoreError IndexDb::upsert_secret(const SecretRow& row) {
  if (handle_ == nullptr) return StoreError::kInternal;
  if (!is_hex32(row.secret_id) || !is_hex32(row.master_key_id) || row.file_path.empty()) {
    return StoreError::kInvalidArgument;
  }
  if (!find_secret(row.secret_id).has_value() && secret_count() >= kMaxSecrets) {
    return StoreError::kQuotaExceededSecrets;
  }

  if (row.created_at == 0) {
    if (const std::optional<SecretRow> old = find_secret(row.secret_id)) {
      SecretRow merged = row;
      merged.created_at = old->created_at;
      return upsert_secret(merged);
    }
  }

  sqlite3_stmt* st = nullptr;
  const std::string sql = std::string(
                              "INSERT INTO secrets (") +
                          kSecretColumns +
                          ") VALUES(?,?,?,?,COALESCE(NULLIF(?,0),?),COALESCE(NULLIF(?,0),?)) "
                          "ON CONFLICT(secret_id) DO UPDATE SET "
                          "master_key_id=excluded.master_key_id, title=excluded.title, "
                          "file_path=excluded.file_path, updated_at=excluded.updated_at";
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(handle_), sql.c_str(), -1, &st, nullptr) !=
      SQLITE_OK) {
    last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
    return StoreError::kInternal;
  }

  const std::int64_t now = now_millis();
  StoreError e = bind_text(st, 1, row.secret_id);
  if (e == StoreError::kOk) e = bind_text(st, 2, row.master_key_id);
  if (e == StoreError::kOk) e = bind_text(st, 3, row.title);
  if (e == StoreError::kOk) e = bind_text(st, 4, row.file_path);
  if (e == StoreError::kOk) e = bind_int64(st, 5, row.created_at);
  if (e == StoreError::kOk) e = bind_int64(st, 6, now);
  if (e == StoreError::kOk) e = bind_int64(st, 7, row.updated_at);
  if (e == StoreError::kOk) e = bind_int64(st, 8, now);
  if (e != StoreError::kOk) {
    sqlite3_finalize(st);
    return e;
  }
  if (sqlite3_step(st) != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
    sqlite3_finalize(st);
    return StoreError::kInternal;
  }
  sqlite3_finalize(st);
  return StoreError::kOk;
}

StoreError IndexDb::delete_master_key(std::string_view master_key_id) {
  if (handle_ == nullptr) return StoreError::kInternal;
  if (!is_hex32(master_key_id)) return StoreError::kInvalidArgument;

  // 注意：secrets 表不做级联删除。需求明确要求删除主密钥时保留机密信息，
  // 这些记录会变成悬空引用，UI 显示黄色问号。
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(handle_),
                         "DELETE FROM master_keys WHERE master_key_id=?", -1, &st,
                         nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
    return StoreError::kInternal;
  }
  bind_text(st, 1, master_key_id);
  const int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
    return StoreError::kInternal;
  }
  if (sqlite3_changes(static_cast<sqlite3*>(handle_)) == 0) return StoreError::kNotFound;

  const std::optional<std::string> def = default_master_key_id();
  if (def && *def == std::string(master_key_id)) {
    // 默认主密钥被删：交给剩余的第一条；没有剩余则清空。
    const std::vector<MasterKeyRow> rest = list_master_keys();
    if (rest.empty()) {
      const StoreError e = exec("DELETE FROM app_meta WHERE key='default_master_key_id';");
      if (e != StoreError::kOk) return e;
    } else {
      const StoreError e = set_default_master_key(rest.front().master_key_id);
      if (e != StoreError::kOk) return e;
    }
  }
  return StoreError::kOk;
}

StoreError IndexDb::delete_secret(std::string_view secret_id) {
  if (handle_ == nullptr) return StoreError::kInternal;
  if (!is_hex32(secret_id)) return StoreError::kInvalidArgument;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(handle_), "DELETE FROM secrets WHERE secret_id=?",
                         -1, &st, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
    return StoreError::kInternal;
  }
  bind_text(st, 1, secret_id);
  const int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
    return StoreError::kInternal;
  }
  return sqlite3_changes(static_cast<sqlite3*>(handle_)) == 0 ? StoreError::kNotFound
                                                            : StoreError::kOk;
}

std::optional<MasterKeyRow> IndexDb::find_master_key(std::string_view master_key_id) const {
  for (const MasterKeyRow& r : list_master_keys()) {
    if (r.master_key_id == master_key_id) return r;
  }
  return std::nullopt;
}

std::optional<SecretRow> IndexDb::find_secret(std::string_view secret_id) const {
  for (const SecretRow& r : list_secrets()) {
    if (r.secret_id == secret_id) return r;
  }
  return std::nullopt;
}

std::vector<MasterKeyRow> IndexDb::list_master_keys() const {
  std::vector<MasterKeyRow> out;
  if (handle_ == nullptr) return out;
  sqlite3_stmt* st = nullptr;
  const std::string sql =
      std::string("SELECT ") + kMasterKeyColumns + " FROM master_keys ORDER BY created_at, master_key_id";
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(handle_), sql.c_str(), -1, &st, nullptr) !=
      SQLITE_OK) {
    return out;
  }
  while (sqlite3_step(st) == SQLITE_ROW) out.push_back(read_master_key_row(st));
  sqlite3_finalize(st);
  return out;
}

std::vector<SecretRow> IndexDb::list_secrets() const {
  std::vector<SecretRow> out;
  if (handle_ == nullptr) return out;
  sqlite3_stmt* st = nullptr;
  const std::string sql =
      std::string("SELECT ") + kSecretColumns + " FROM secrets ORDER BY created_at, secret_id";
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(handle_), sql.c_str(), -1, &st, nullptr) !=
      SQLITE_OK) {
    return out;
  }
  while (sqlite3_step(st) == SQLITE_ROW) out.push_back(read_secret_row(st));
  sqlite3_finalize(st);
  return out;
}

std::vector<SecretRow> IndexDb::list_secrets_by_master_key(std::string_view master_key_id) const {
  std::vector<SecretRow> out;
  if (handle_ == nullptr) return out;
  sqlite3_stmt* st = nullptr;
  const std::string sql = std::string("SELECT ") + kSecretColumns +
                          " FROM secrets WHERE master_key_id=? ORDER BY created_at, secret_id";
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(handle_), sql.c_str(), -1, &st, nullptr) !=
      SQLITE_OK) {
    return out;
  }
  bind_text(st, 1, master_key_id);
  while (sqlite3_step(st) == SQLITE_ROW) out.push_back(read_secret_row(st));
  sqlite3_finalize(st);
  return out;
}

std::optional<std::string> IndexDb::default_master_key_id() const {
  const std::optional<std::string> v = meta("default_master_key_id");
  if (!v || v->empty()) return std::nullopt;
  return v;
}

StoreError IndexDb::set_default_master_key(std::string_view master_key_id) {
  if (!is_hex32(master_key_id)) return StoreError::kInvalidArgument;
  const std::optional<MasterKeyRow> target = find_master_key(master_key_id);
  if (!target) return StoreError::kNotFound;
  // 直接走 upsert_master_key 的默认标记路径：它会在一个事务里清掉其他
  // 记录的 is_default 并同步 app_meta，避免这里再写一遍造成两个真相源。
  MasterKeyRow updated = *target;
  updated.is_default = true;
  updated.created_at = target->created_at;
  return upsert_master_key(updated);
}

std::size_t IndexDb::count_secrets_for_master_key(std::string_view master_key_id) const {
  return list_secrets_by_master_key(master_key_id).size();
}

std::size_t IndexDb::master_key_count() const { return list_master_keys().size(); }

std::size_t IndexDb::secret_count() const { return list_secrets().size(); }

std::optional<std::string> IndexDb::meta(std::string_view key) const {
  if (handle_ == nullptr) return std::nullopt;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(handle_), "SELECT value FROM app_meta WHERE key=?",
                         -1, &st, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  bind_text(st, 1, key);
  std::optional<std::string> out;
  if (sqlite3_step(st) == SQLITE_ROW) out = column_text(st, 0);
  sqlite3_finalize(st);
  return out;
}

StoreError IndexDb::set_meta(std::string_view key, std::string_view value) {
  if (handle_ == nullptr) return StoreError::kInternal;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(handle_),
                         "INSERT INTO app_meta(key, value) VALUES(?,?) "
                         "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                         -1, &st, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
    return StoreError::kInternal;
  }
  bind_text(st, 1, key);
  bind_text(st, 2, value);
  if (sqlite3_step(st) != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(static_cast<sqlite3*>(handle_));
    sqlite3_finalize(st);
    return StoreError::kInternal;
  }
  sqlite3_finalize(st);
  return StoreError::kOk;
}

}  // namespace secretkeeper::store
