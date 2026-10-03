#pragma once

// 机密心 - 索引库（SQLite）
//
// 实现 docs/03-data §6 的表结构。索引库只保存 ID、名称、标题、路径等元数据，
// **不保存任何密钥材料、密码、盐或明文机密信息**——索引库泄露不应导致机密泄露。
//
// 数据文件本身不在本层管理，由 file_store 负责；本层只维护指向它们的索引。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace secretkeeper::store {

// 限额来自需求：2 个主密钥、15 条机密信息、单条 150 字符。
inline constexpr std::size_t kMaxMasterKeys = 2;
inline constexpr std::size_t kMaxSecrets = 15;

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

// 错误类型。限额类错误必须能被 UI 映射到需求指定的中文提示，
// 因此单独区分，不与存储故障混为一谈。
enum class StoreError {
  kOk = 0,
  kNotFound,
  kQuotaExceededMasterKeys,
  kQuotaExceededSecrets,
  kInvalidArgument,
  kBusy,
  kIoError,
  kInternal,
};

const char* to_string(StoreError e);

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

}  // namespace secretkeeper::store
