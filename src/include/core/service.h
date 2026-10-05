#pragma once

// 机密心 - 服务层（core 总入口）
//
// 这是 UI 层唯一允许包含的 core 头文件。UI 不得绕过本层直接调用
// crypto / serialize / store 中的任何符号，也不得接触 crypto::Kek 等
// 密钥类型——所有涉及密钥的操作都在本层内部完成。
//
// 依赖方向（见 AGENTS.md 铁律）：
//     service  ->  store  ->  serialize
//             ->  crypto
//   serialize 与 crypto 互不依赖；store 只依赖 serialize。
//
// 密钥所有权：默认主密钥的 KEK 常驻本层的 KekCache（经程序随机密钥包裹、
// 3 个诱饵槽位随机迁移）。UI 永远拿不到 KEK，只能通过
// unlock() / lock() / 各业务操作间接使用。

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace secretkeeper::service {

// ===========================================================================
// 限额
//
// 业务规则，不属于存储层。v1.0.0 由 License 激活状态覆盖这些值；
// 当前实现直接返回固定值，集中在此处，v1.0.0 只改这一处。
// ===========================================================================

inline constexpr std::size_t kMaxMasterKeys = 2;
inline constexpr std::size_t kMaxSecrets = 15;
inline constexpr std::size_t kMaxSecretChars = 150;

// ===========================================================================
// 错误与提示文案
// ===========================================================================

enum class Error {
  kOk = 0,

  // 密码与解锁
  kPasswordWrong,            // 主密钥密码错误
  kRateLimited,              // 退避等待中（剩余秒数见 backoff()）
  kNeedsPassword,            // 该操作需要非默认主密钥密码，UI 应弹框后带密码重试
  kLocked,                   // 尚未解锁

  // 主密钥
  kMasterKeyQuotaExceeded,   // 主密钥数量已达上限
  kMasterKeyIsDefault,       // 禁止删除默认主密钥
  kMasterKeyNotFound,        // 主密钥不存在
  kCannotDeleteDefault,      // 不能删除默认主密钥

  // 机密信息
  kSecretQuotaExceeded,      // 机密信息条数已达上限
  kSecretTooLong,            // 超出 150 字符
  kSecretNotFound,
  kEmptySecret,              // 内容为空

  // 导入导出
  kImportBadFormat,          // 格式非法 / Magic 不匹配
  kImportVersionTooHigh,     // 文件版本过高
  kImportMasterKeyMissing,   // 请先导入主密钥
  kImportDecryptFailed,      // 解密错误
  kImportSaveFailed,         // 保存出错
  kImportConflict,           // ID 冲突（内部保留，一般自动改 ID）

  // 环境与存储
  kIoError,
  kCryptoUnsupported,        // 环境缺少所需算法能力
  kInternal,
};

// 需求文档中固定的中文提示文案。UI 必须原样显示，不得改写。
// 生命周期为 static，调用方无需管理。
std::string_view message(Error e);

// 是否属于「密码错误」类错误。只有这类才计入连续失败次数与退避。
bool is_password_error(Error e);

// 需求 3.5：主密钥缺失时详情页显示的掩码。固定 6 个星号，UI 不得自造。
extern const char* const kMaskedPlaintext;

// ===========================================================================
// DTO —— UI 只看到这些，不看到行对象、字节流或格式结构
// ===========================================================================

struct MasterKeyListItem {
  std::string master_key_id;
  std::string name;           // 可为空，且为空**不代表**该密钥不存在
  bool is_default = false;
  std::size_t secret_count = 0;
};

struct SecretListItem {
  std::string secret_id;
  std::string title;          // 可为空
  std::string master_key_id;
  std::string master_key_name;
  // 驱动主密钥 ID 列后的黄色问号。与 name 是否为空**完全无关**：
  // 密钥存在但名称为空 -> found=true, name="" -> 无问号；
  // 密钥缺失         -> found=false          -> 有问号。
  bool master_key_found = false;
};

struct SecretDetail {
  std::string secret_id;
  std::string title;
  std::string master_key_id;
  std::string master_key_name;
  bool master_key_found = false;
  // 主密钥缺失或尚未提供密码时为 true，UI 显示 6 个 * 掩码。
  bool plaintext_masked = true;
  // 仅当 plaintext_masked == false 时有意义。
  std::string plaintext;
};

struct QuotaStatus {
  std::size_t master_keys = 0;
  std::size_t max_master_keys = kMaxMasterKeys;
  std::size_t secrets = 0;
  std::size_t max_secrets = kMaxSecrets;
};

struct BackoffStatus {
  bool waiting = false;
  std::uint32_t remaining_seconds = 0;
  std::uint32_t consecutive_failures = 0;
};

// ===========================================================================
// Service
// ===========================================================================

class Service {
 public:
  Service();
  ~Service();
  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;
  Service(Service&&) = delete;
  Service& operator=(Service&&) = delete;

  // ---- 生命周期 ----

  // 建目录并打开索引库。data_dir 为空时由调用方先取平台数据目录。
  Error open(const std::string& data_dir);
  void close();
  bool is_open() const;

  // ---- 会话与锁定 ----

  // 用指定主密钥的密码解锁。成功后 KEK 进入缓存，并触发一次轮换（需求 4.2）。
  Error unlock(std::string_view master_key_id, std::span<const std::uint8_t> password);
  // 锁定：清空 KEK 缓存与退避计数。
  void lock();
  bool is_unlocked() const;

  // KEK 轮换（需求 2.7 的 30 秒定时器）。未解锁时返回 kLocked。
  Error rotate_kek();
  // 记录一次用户活动，供 UI 的闲置锁定计时使用。
  void notify_activity();

  // 退避状态。UI 用它显示「N 秒后可解锁」。
  BackoffStatus backoff() const;

  // ---- 主密钥（需求 §2 的六项操作）----

  std::vector<MasterKeyListItem> list_master_keys() const;

  // 生成一把新主密钥。第一把自动成为默认主密钥。
  Error create_master_key(std::string_view name, std::span<const std::uint8_t> password);

  // 导出主密钥。password 为空且目标是默认主密钥时用缓存的 KEK；
  // 否则必须提供该密钥的密码，否则返回 kNeedsPassword。
  Error export_master_key(std::string_view master_key_id,
                          std::span<const std::uint8_t> password,
                          std::vector<std::uint8_t>* out);

  // 导入主密钥：用 protection_password 解开文件，再用 new_password 重加密保存。
  // 导入的密钥不自动成为默认主密钥。
  Error import_master_key(std::span<const std::uint8_t> bytes,
                          std::span<const std::uint8_t> protection_password,
                          std::span<const std::uint8_t> new_password);

  // 切换默认主密钥。需求 2.5 要求**同时**验证原默认与新默认两把密钥的密码。
  // 已解锁时两者都能用缓存 KEK 校验，两个密码参数都可为空；未解锁时
  // 至少需要提供其中一个，否则返回 kNeedsPassword。
  Error switch_default_master_key(std::string_view master_key_id,
                                  std::span<const std::uint8_t> current_password,
                                  std::span<const std::uint8_t> new_password);
  Error delete_master_key(std::string_view master_key_id,
                          std::span<const std::uint8_t> password);
  Error verify_master_key_password(std::string_view master_key_id,
                                   std::span<const std::uint8_t> password);

  // ---- 机密信息（需求 §3 的六项操作）----

  std::vector<SecretListItem> list_secrets() const;

  // 新增一条机密信息。master_password 为空且目标是默认主密钥时用缓存的 KEK。
  Error add_secret(std::string_view title, std::string_view plaintext,
                   std::string_view master_key_id,
                   std::span<const std::uint8_t> master_password);

  Error export_secret(std::string_view secret_id,
                      std::span<const std::uint8_t> master_password,
                      std::vector<std::uint8_t>* out);

  // 导入机密信息文件。file_password 解开文件，master_password 解开数据密钥。
  Error import_secret(std::span<const std::uint8_t> bytes,
                      std::span<const std::uint8_t> file_password,
                      std::span<const std::uint8_t> master_password);

  // 详情页。master_password 为空时返回掩码态（plaintext_masked = true），
  // 不返回 kNeedsPassword —— 这样 UI 可以先展示列表再决定是否索要密码。
  Error get_secret_detail(std::string_view secret_id,
                          std::span<const std::uint8_t> master_password,
                          SecretDetail* out);

  // 「查看」明文。默认主密钥用缓存 KEK；非默认主密钥需密码，
  // 否则返回 kNeedsPassword。
  Error reveal_secret_plaintext(std::string_view secret_id,
                                std::span<const std::uint8_t> master_password,
                                std::string* out);

  Error delete_secret(std::string_view secret_id,
                      std::span<const std::uint8_t> master_password);

  // ---- 杂项 ----

  QuotaStatus quota() const;
  std::optional<std::string> default_master_key_id() const;

  // 单条机密信息的字符数（按 Unicode 码点计），供 UI 显示「n / 150」。
  // 放在本层是因为 150 这个上限是业务规则。
  static std::size_t secret_char_count(std::string_view plaintext);

 private:
  struct Impl;
  Impl* impl_;  // 不完整类型，保持本头文件不外泄 core 内部结构
};

}  // namespace secretkeeper::service
