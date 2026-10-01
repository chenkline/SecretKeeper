#pragma once

// 机密心 - 主密钥业务逻辑
//
// 实现 docs/04-requirements §2 的六项操作：查询、生成、导出、导入、切换、删除。
// 本层只做编排，把「派生 KEK → 生成密钥对 → 加密 → 落盘 → 更新索引」
// 这些步骤串起来；具体的字节操作全部委托给 crypto / container / store 层。
//
// 密码处理原则：密码以字节形式传入，本层不持有密码的副本，也不把它写进
// 任何结构或日志。KEK 派生完成后立即清零。

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "../container/container.h"
#include "../crypto/crypto.h"
#include "../store/file_store.h"
#include "../store/index_db.h"
#include "error.h"

namespace secretkeeper::core {

// 列表项。master_key_missing 为真时，UI 需在主密钥 ID 后显示黄色问号。
// 注意：黄色问号与 name 是否为空**无关**，两者必须能独立表达。
struct MasterKeyListItem {
  std::string master_key_id;
  std::string name;
  bool is_default = false;
};

class MasterKeyService {
 public:
  MasterKeyService(store::IndexDb& db, store::FileStore& files) : db_(db), files_(files) {}

  // §2.1 查询主密钥列表
  std::vector<MasterKeyListItem> list() const;

  // §2.2 生成主密钥。name 可为空。is_first 由调用方根据现有数量决定，
  // 为真则自动设为默认主密钥。
  Error create(std::string_view name, std::span<const std::uint8_t> password, bool is_first);

  // §2.3 导出主密钥：用**独立的保护密码**与**新盐**重新加密公私钥，
  // 输出与本地同格式的字节流，扩展名 .smkexp。
  //
  // 注意 kek 是**本地记录的 KEK**（由本地主密钥密码派生，通常来自 KEK 缓存），
  // protection_password 是**用户新设的保护密码**，两者角色不同不可混用。
  Error export_to(std::string_view master_key_id, const crypto::Kek& kek,
                  std::span<const std::uint8_t> protection_password,
                  std::vector<std::uint8_t>* out);

  // §2.4 导入主密钥：用保护密码解开公私钥，再用**新的主密钥密码**与
  // **新盐、两个新 nonce** 重新加密保存。导入的密钥不自动成为默认主密钥。
  Error import_from(std::span<const std::uint8_t> export_bytes,
                    std::span<const std::uint8_t> protection_password,
                    std::span<const std::uint8_t> new_password);

  // §2.5 切换默认主密钥：需同时验证原默认主密钥密码与新默认主密钥密码。
  // 两个密码相同时只验证一次（用户可能就是同一个）。
  Error switch_default(std::string_view new_default_id,
                       std::span<const std::uint8_t> old_password,
                       std::span<const std::uint8_t> new_password);

  // §2.6 删除主密钥：禁止删除默认主密钥；不删除其机密信息（形成悬空引用）。
  Error remove(std::string_view master_key_id, std::span<const std::uint8_t> password);

  // 验证密码是否正确（用于 UI 的「密码确认」步骤），不改变任何状态。
  Error verify_password(std::string_view master_key_id,
                        std::span<const std::uint8_t> password);

  // 解出主密钥的公钥 DER（用于封装数据密钥）。需 KEK。
  Error public_key_der(std::string_view master_key_id, const crypto::Kek& kek,
                       crypto::SecureBytes* out) const;

  // 解出主密钥的私钥 DER（用于解封装数据密钥）。需 KEK。
  Error private_key_der(std::string_view master_key_id, const crypto::Kek& kek,
                        crypto::SecureBytes* out) const;

  // 读取并解析主密钥数据文件。供上层复用。
  Error load_file(std::string_view master_key_id, container::MasterKeyFile* out) const;

 private:
  store::IndexDb& db_;
  store::FileStore& files_;
};

}  // namespace secretkeeper::core
