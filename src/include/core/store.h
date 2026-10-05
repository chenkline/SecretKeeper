#pragma once

// 机密心 - 存储层
//
// 本层负责两件事，且只做这两件事：
//   1. SQLite 索引库：保存 ID、名称、标题、路径等**元数据**；
//   2. 数据文件读写：按 docs/03-data §6.1 的目录布局读写字节流。
//
// **本层不含任何密钥材料、密码、盐或明文机密信息**——索引库泄露不应导致机密泄露。
//
// 依赖方向（见 AGENTS.md 铁律）：store 只依赖 serialize。
// 字节流 <-> 内存对象的转换由 serialize 完成，本层不实现任何格式细节；
// MasterKeyStore / SecretStore 在本层把 serialize 与文件读写粘合起来，
// 对上层只暴露内存对象，不暴露裸字节。
//
// 限额不在本层：2 主密钥 / 15 条机密信息 / 150 字符是**业务规则**，
// 由 service 层判定。本层只如实报告计数（count_*）。

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "serialize.h"

namespace secretkeeper::store {

// ===========================================================================
// 十六进制 ID 与字节 ID 的互转
//
// 索引库以 32 位小写十六进制字符串作主键，数据文件与 GCM AAD 用原始 16 字节。
// 转换必须严格：奇数长度、非法字符一律拒绝，不做静默截断或补零。
// ===========================================================================

using serialize::Id;

inline constexpr std::size_t kHexCharsPerId = serialize::kIdLength * 2;  // 32

// 成功返回 true；失败时 out 不被修改。
bool id_from_hex(std::string_view hex, Id* out);
bool id_to_hex(const Id& id, std::string* out);

// 任意长度的字节/十六进制互转，供测试与调试使用。
bool bytes_from_hex(std::string_view hex, std::vector<std::uint8_t>* out);
std::string bytes_to_hex(std::span<const std::uint8_t> bytes);

// ===========================================================================
// 索引库行对象
// ===========================================================================

// 列表项。时间戳为 Unix 毫秒。
struct MasterKeyRow {
  std::string master_key_id;  // 32 位小写十六进制
  std::string name;           // 可为空
  std::string file_path;      // 相对数据目录
  std::int64_t mk_alg = 1;    // 1 = RSA-2048
  bool is_default = false;
  std::int64_t created_at = 0;
  std::int64_t updated_at = 0;
};

struct SecretRow {
  std::string secret_id;
  std::string master_key_id;  // 允许悬空：主密钥可能已被删除
  std::string title;          // 可为空
  std::string file_path;
  std::int64_t created_at = 0;
  std::int64_t updated_at = 0;
};

// 错误类型。**不含限额类错误**：限额是业务规则，由 service 判定。
// 存储故障与「记录不存在」必须能区分——前者提示稍后重试，后者提示先导入。
enum class StoreError {
  kOk = 0,
  kNotFound,
  kInvalidArgument,
  kBusy,
  kIoError,
  kInternal,
};

const char* to_string(StoreError e);

// ===========================================================================
// 索引库（SQLite）
// ===========================================================================

// 非拥有的数据库句柄。IndexDb 不负责 sqlite3_close 之外的任何全局状态，
// 也不允许拷贝——两个句柄指向同一文件会放大出错面。
class IndexDb {
 public:
  IndexDb() = default;
  IndexDb(const IndexDb&) = delete;
  IndexDb& operator=(const IndexDb&) = delete;
  IndexDb(IndexDb&&) noexcept;
  IndexDb& operator=(IndexDb&&) noexcept;
  ~IndexDb();

  // 打开或创建索引库，并建表。db_path 指向数据库文件本身。
  StoreError open(const std::string& db_path);
  void close();
  bool is_open() const noexcept { return handle_ != nullptr; }

  // upsert 语义：ID 已存在则更新，不存在则插入，且保留原有 created_at。
  // **不做限额判定**——调用方（service）必须先自行检查限额。
  StoreError upsert_master_key(const MasterKeyRow& row);
  StoreError upsert_secret(const SecretRow& row);

  StoreError delete_master_key(std::string_view master_key_id);
  StoreError delete_secret(std::string_view secret_id);

  std::optional<MasterKeyRow> find_master_key(std::string_view master_key_id) const;
  std::optional<SecretRow> find_secret(std::string_view secret_id) const;

  std::vector<MasterKeyRow> list_master_keys() const;
  std::vector<SecretRow> list_secrets() const;
  std::vector<SecretRow> list_secrets_by_master_key(std::string_view master_key_id) const;

  // 默认主密钥。未设置时返回 nullopt。
  std::optional<std::string> default_master_key_id() const;
  StoreError set_default_master_key(std::string_view master_key_id);

  // 指向该主密钥的机密信息条数；用于删除前的影响面提示。
  std::size_t count_secrets_for_master_key(std::string_view master_key_id) const;

  // 纯计数。是否超限由 service 判定。
  std::size_t master_key_count() const;
  std::size_t secret_count() const;

  std::optional<std::string> meta(std::string_view key) const;
  StoreError set_meta(std::string_view key, std::string_view value);

  const char* last_error() const noexcept { return last_error_; }

 private:
  StoreError exec(const char* sql) const;
  StoreError create_schema() const;

  void* handle_ = nullptr;  // sqlite3*，以 void* 避免头文件外泄 SQLite 类型
  // SQLite 的 errmsg 返回的是内部缓冲，生命周期与连接绑定，因此这里存指针
  // 而非拷贝字符串。mutable 允许在 const 查询方法中记录错误。
  mutable const char* last_error_ = "";
};

// ===========================================================================
// 数据文件读写（裸字节）
//
// 目录布局对应 docs/03-data §6.1：
//   <data_dir>/secret.db         索引库（由 IndexDb 管理）
//   <data_dir>/keys/<id>.smk    主密钥数据文件
//   <data_dir>/secrets/<id>.ssc 机密信息数据文件
//
// 文件名完全由 ID 派生，不含随机性，便于人工排查与备份。
// ===========================================================================

class FileStore {
 public:
  FileStore() = default;
  explicit FileStore(std::string data_dir) : data_dir_(std::move(data_dir)) {}

  const std::string& data_dir() const noexcept { return data_dir_; }
  void set_data_dir(std::string dir) { data_dir_ = std::move(dir); }

  // 建目录并做基本的可写性自检。已存在也算成功。
  bool ensure_layout(std::string* error) const;

  // 主密钥数据文件的相对路径，形如 keys/<32hex>.smk。
  static std::string master_key_rel_path(std::string_view master_key_id_hex);
  static std::string secret_rel_path(std::string_view secret_id_hex);

  bool master_key_path(std::string_view master_key_id_hex, std::string* out) const;
  bool secret_path(std::string_view secret_id_hex, std::string* out) const;

  // 写入采用「先写临时文件再原子改名」，避免崩溃或断电留下半截文件
  // 被后续读取当成合法记录。
  bool write_atomic(const std::string& rel_path, std::span<const std::uint8_t> bytes,
                    std::string* error) const;
  bool read(const std::string& rel_path, std::vector<std::uint8_t>* out,
            std::string* error) const;
  bool remove(const std::string& rel_path, std::string* error) const;

 private:
  bool ensure_dir(const std::string& path, std::string* error) const;

  std::string data_dir_;
};

// ===========================================================================
// 对象级存储（在 FileStore + serialize 之上）
//
// 这两个类是 store 层对 service 暴露的**唯一**数据落盘入口：
// service 只见内存对象，既不碰字节流，也不碰格式细节。
// ===========================================================================

enum class SaveError {
  kOk = 0,
  kInvalidArgument,  // ID 非法 / 字段缺失
  kIoError,           // 目录不可写、磁盘满、文件被占用
  kParseError,        // 读取时格式非法（详见 serialize::ParseError）
  kNotFound,          // 记录或文件不存在
  kInternal,          // 索引登记失败
};

// 保存并登记索引。file_path 由本层按 ID 派生后写入 row.file_path。
// quota_not_exceeded 由调用方保证：本层不做限额判定。
class MasterKeyStore {
 public:
  MasterKeyStore(IndexDb& db, FileStore& files) : db_(db), files_(files) {}

  SaveError save(const serialize::MasterKeyFile& file, const std::string& name,
                 bool make_default);
  SaveError load(std::string_view master_key_id_hex, serialize::MasterKeyFile* out) const;

 private:
  IndexDb& db_;
  FileStore& files_;
};

class SecretStore {
 public:
  SecretStore(IndexDb& db, FileStore& files) : db_(db), files_(files) {}

  SaveError save(const serialize::SecretFile& file);
  SaveError load(std::string_view secret_id_hex, serialize::SecretFile* out) const;

 private:
  IndexDb& db_;
  FileStore& files_;
};

}  // namespace secretkeeper::store
