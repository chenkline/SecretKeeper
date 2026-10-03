#include "master_key_service.h"

#include <cstring>

#include "hex.h"

namespace secretkeeper::core {
namespace {

// 记录的四项加密密钥：公钥与私钥各自独立 nonce。
struct SealedKeys {
  crypto::Nonce pub_nonce{};
  crypto::Tag pub_tag{};
  std::vector<std::uint8_t> pub_cipher;
  crypto::Nonce priv_nonce{};
  crypto::Tag priv_tag{};
  std::vector<std::uint8_t> priv_cipher;
};

// 公私钥的 nonce 必须不同。若复用同一 nonce，GCM 密钥流完全相同，
// 攻击者无需任何密钥即可由 pubKeyCipher XOR privKeyCipher 恢复两者异或值，
// 机密性彻底失效。因此这里显式校验并重试。
bool seal_keys(const crypto::Kek& kek, std::span<const std::uint8_t> aad,
               std::span<const std::uint8_t> pub_der,
               std::span<const std::uint8_t> priv_der, SealedKeys* out) {
  for (int attempt = 0; attempt < 8; ++attempt) {
    crypto::random_bytes(std::span<std::uint8_t>(out->pub_nonce));
    crypto::random_bytes(std::span<std::uint8_t>(out->priv_nonce));
    if (out->pub_nonce != out->priv_nonce) break;
    if (attempt == 7) return false;
  }
  out->pub_cipher.assign(pub_der.size() + crypto::kTagLength, 0);
  out->priv_cipher.assign(priv_der.size() + crypto::kTagLength, 0);
  crypto::aes_gcm_encrypt(kek.data(), kek.size(), out->pub_nonce, aad, pub_der,
                          out->pub_cipher.data(), out->pub_tag.data());
  crypto::aes_gcm_encrypt(kek.data(), kek.size(), out->priv_nonce, aad, priv_der,
                          out->priv_cipher.data(), out->priv_tag.data());
  return true;
}

// 公钥与私钥的 AAD 都是 masterKeyId 的原始 16 字节。
constexpr std::size_t kCipherTail = crypto::kTagLength;

std::span<const std::uint8_t> cipher_body(std::span<const std::uint8_t> v) {
  return v.subspan(0, v.size() - kCipherTail);
}

// Length of the plaintext that cipher_body() recovers. AES-GCM is a stream mode:
// the ciphertext is exactly as long as the plaintext, so the stored vector is
// (plaintext || tag) and the plaintext length is (size - tag). Sizing the output
// buffer from the vector length instead would append kTagLength zero bytes to the
// recovered DER, and the RSA parser rejects such a trailing-garbage key.
std::size_t plain_size(std::span<const std::uint8_t> cipher) {
  return cipher.size() - kCipherTail;
}

}  // namespace

std::vector<MasterKeyListItem> MasterKeyService::list() const {
  std::vector<MasterKeyListItem> out;
  for (const store::MasterKeyRow& row : db_.list_master_keys()) {
    MasterKeyListItem item;
    item.master_key_id = row.master_key_id;
    item.name = row.name;
    item.is_default = row.is_default ||
                      db_.default_master_key_id().value_or("") == row.master_key_id;
    out.push_back(std::move(item));
  }
  return out;
}

Error MasterKeyService::load_file(std::string_view master_key_id,
                                  container::MasterKeyFile* out) const {
  const std::optional<store::MasterKeyRow> row = db_.find_master_key(master_key_id);
  if (!row) return Error::kMasterKeyNotFound;

  std::vector<std::uint8_t> bytes;
  std::string io_err;
  if (!files_.read(row->file_path, &bytes, &io_err)) return Error::kIoError;

  container::ParseError perr = container::ParseError::kOk;
  auto parsed = container::parse_master_key(bytes, &perr);
  if (!parsed) {
    switch (perr) {
      case container::ParseError::kUnsupportedVersion: return Error::kImportVersionTooHigh;
      case container::ParseError::kBadMagic:
      case container::ParseError::kTruncated:
      case container::ParseError::kLengthMismatch:
      case container::ParseError::kUnknownAlgorithm:
      case container::ParseError::kInvalidUtf8:
      default: return Error::kImportBadFormat;
    }
  }
  *out = std::move(*parsed);
  return Error::kOk;
}

Error MasterKeyService::create(std::string_view name, std::span<const std::uint8_t> password,
                               bool is_first) {
  if (password.empty()) return Error::kPasswordWrong;
  if (db_.master_key_count() >= store::kMaxMasterKeys) return Error::kMasterKeyQuotaExceeded;
  if (!crypto::has_asymmetric_support()) return Error::kCryptoUnsupported;

  const crypto::Id id = crypto::generate_id();
  crypto::Salt salt{};
  crypto::random_bytes(std::span<std::uint8_t>(salt));
  const crypto::Kek kek = crypto::derive_kek(password, salt);

  crypto::RsaKeyPair pair = crypto::generate_rsa2048();

  SealedKeys sealed;
  const bool ok = seal_keys(kek, std::span<const std::uint8_t>(id),
                            std::span<const std::uint8_t>(pair.public_der.data(), pair.public_der.size()),
                            std::span<const std::uint8_t>(pair.private_der.data(),
                                                           pair.private_der.size()),
                            &sealed);

  container::MasterKeyFile f;
  f.master_key_id = id;
  f.name = std::string(name);
  f.salt = salt;
  f.kdf_mem = crypto::kKdfMemoryKiB;
  f.kdf_iter = crypto::kKdfIterations;
  f.kdf_par = crypto::kKdfParallelism;
  f.pub_key_nonce = sealed.pub_nonce;
  f.pub_key_tag = sealed.pub_tag;
  f.pub_key_cipher = std::move(sealed.pub_cipher);
  f.priv_key_nonce = sealed.priv_nonce;
  f.priv_key_tag = sealed.priv_tag;
  f.priv_key_cipher = std::move(sealed.priv_cipher);

  const std::string hex_id = store::bytes_to_hex(std::span<const std::uint8_t>(id));
  const std::string rel = store::FileStore::master_key_rel_path(hex_id);

  std::string io_err;
  const std::vector<std::uint8_t> bytes = container::serialize(f);
  if (!files_.write_atomic(rel, bytes, &io_err)) return Error::kIoError;

  store::MasterKeyRow row;
  row.master_key_id = hex_id;
  row.name = std::string(name);
  row.file_path = rel;
  row.mk_alg = container::kAlgRsa2048;
  row.is_default = is_first;
  const store::StoreError se = db_.upsert_master_key(row);
  if (se == store::StoreError::kQuotaExceededMasterKeys) {
    files_.remove(rel, &io_err);
    return Error::kMasterKeyQuotaExceeded;
  }
  if (se != store::StoreError::kOk) {
    files_.remove(rel, &io_err);
    return Error::kIoError;
  }
  return Error::kOk;
}

Error MasterKeyService::public_key_der(std::string_view master_key_id, const crypto::Kek& kek,
                                       crypto::SecureBytes* out) const {
  container::MasterKeyFile f;
  const Error e = load_file(master_key_id, &f);
  if (e != Error::kOk) return e;

  crypto::SecureBytes plain(plain_size(f.pub_key_cipher));
  const crypto::CryptoError rc = crypto::aes_gcm_decrypt(
      kek.data(), kek.size(), f.pub_key_nonce, std::span<const std::uint8_t>(f.master_key_id),
      cipher_body(f.pub_key_cipher), f.pub_key_tag, plain.data());
  if (rc != crypto::CryptoError::kOk) return Error::kPasswordWrong;

  *out = std::move(plain);
  return Error::kOk;
}

Error MasterKeyService::private_key_der(std::string_view master_key_id, const crypto::Kek& kek,
                                        crypto::SecureBytes* out) const {
  container::MasterKeyFile f;
  const Error e = load_file(master_key_id, &f);
  if (e != Error::kOk) return e;

  crypto::SecureBytes plain(plain_size(f.priv_key_cipher));
  const crypto::CryptoError rc = crypto::aes_gcm_decrypt(
      kek.data(), kek.size(), f.priv_key_nonce, std::span<const std::uint8_t>(f.master_key_id),
      cipher_body(f.priv_key_cipher), f.priv_key_tag, plain.data());
  if (rc != crypto::CryptoError::kOk) return Error::kPasswordWrong;

  *out = std::move(plain);
  return Error::kOk;
}

Error MasterKeyService::verify_password(std::string_view master_key_id,
                                        std::span<const std::uint8_t> password) {
  container::MasterKeyFile f;
  const Error e = load_file(master_key_id, &f);
  if (e != Error::kOk) return e;

  const crypto::Kek kek = crypto::derive_kek(password, f.salt);
  crypto::SecureBytes probe;
  // 只解公钥即可判定密码正确性，不必付出解私钥的代价。
  return public_key_der(master_key_id, kek, &probe);
}

Error MasterKeyService::export_to(std::string_view master_key_id, const crypto::Kek& kek,
                                  std::span<const std::uint8_t> protection_password,
                                  std::vector<std::uint8_t>* out) {
  if (protection_password.empty()) return Error::kPasswordWrong;

  container::MasterKeyFile f;
  const Error e = load_file(master_key_id, &f);
  if (e != Error::kOk) return e;

  crypto::SecureBytes pub_plain(plain_size(f.pub_key_cipher));
  crypto::SecureBytes priv_plain(plain_size(f.priv_key_cipher));
  if (crypto::aes_gcm_decrypt(kek.data(), kek.size(), f.pub_key_nonce,
                              std::span<const std::uint8_t>(f.master_key_id),
                              cipher_body(f.pub_key_cipher), f.pub_key_tag,
                              pub_plain.data()) != crypto::CryptoError::kOk) {
    return Error::kPasswordWrong;
  }
  if (crypto::aes_gcm_decrypt(kek.data(), kek.size(), f.priv_key_nonce,
                              std::span<const std::uint8_t>(f.master_key_id),
                              cipher_body(f.priv_key_cipher), f.priv_key_tag,
                              priv_plain.data()) != crypto::CryptoError::kOk) {
    return Error::kPasswordWrong;
  }

  // docs/03-data §7.3：导出使用新盐，保护密码派生的新 KEK，公私钥各自新 nonce。
  // masterKeyId 与 name 与本地一致；公钥明文不变，但因保护密码不同，密文必然不同。
  crypto::Salt salt{};
  crypto::random_bytes(std::span<std::uint8_t>(salt));
  const crypto::Kek prot_kek = crypto::derive_kek(protection_password, salt);

  SealedKeys sealed;
  if (!seal_keys(prot_kek, std::span<const std::uint8_t>(f.master_key_id),
                 std::span<const std::uint8_t>(pub_plain.data(), pub_plain.size()),
                 std::span<const std::uint8_t>(priv_plain.data(), priv_plain.size()),
                 &sealed)) {
    return Error::kInternal;
  }

  container::MasterKeyFile dst;
  dst.master_key_id = f.master_key_id;
  dst.name = f.name;
  dst.salt = salt;
  dst.kdf_mem = crypto::kKdfMemoryKiB;
  dst.kdf_iter = crypto::kKdfIterations;
  dst.kdf_par = crypto::kKdfParallelism;
  dst.pub_key_nonce = sealed.pub_nonce;
  dst.pub_key_tag = sealed.pub_tag;
  dst.pub_key_cipher = std::move(sealed.pub_cipher);
  dst.priv_key_nonce = sealed.priv_nonce;
  dst.priv_key_tag = sealed.priv_tag;
  dst.priv_key_cipher = std::move(sealed.priv_cipher);

  *out = container::serialize(dst);
  return Error::kOk;
}

Error MasterKeyService::import_from(std::span<const std::uint8_t> export_bytes,
                                    std::span<const std::uint8_t> protection_password,
                                    std::span<const std::uint8_t> new_password) {
  container::ParseError perr = container::ParseError::kOk;
  auto parsed = container::parse_master_key(export_bytes, &perr);
  if (!parsed) {
    switch (perr) {
      case container::ParseError::kUnsupportedVersion: return Error::kImportVersionTooHigh;
      default: return Error::kImportBadFormat;
    }
  }
  if (db_.master_key_count() >= store::kMaxMasterKeys) return Error::kMasterKeyQuotaExceeded;

  container::MasterKeyFile src = std::move(*parsed);
  const crypto::Kek prot_kek = crypto::derive_kek(protection_password, src.salt);

  crypto::SecureBytes pub_plain(plain_size(src.pub_key_cipher));
  crypto::SecureBytes priv_plain(plain_size(src.priv_key_cipher));
  if (crypto::aes_gcm_decrypt(prot_kek.data(), prot_kek.size(), src.pub_key_nonce,
                              std::span<const std::uint8_t>(src.master_key_id),
                              cipher_body(src.pub_key_cipher), src.pub_key_tag,
                              pub_plain.data()) != crypto::CryptoError::kOk) {
    return Error::kPasswordWrong;
  }
  if (crypto::aes_gcm_decrypt(prot_kek.data(), prot_kek.size(), src.priv_key_nonce,
                              std::span<const std::uint8_t>(src.master_key_id),
                              cipher_body(src.priv_key_cipher), src.priv_key_tag,
                              priv_plain.data()) != crypto::CryptoError::kOk) {
    return Error::kPasswordWrong;
  }

  // ID 冲突时重新生成，保证「导出→导入」往返一致且不覆盖本地记录。
  crypto::Id id = src.master_key_id;
  std::string hex_id = store::bytes_to_hex(std::span<const std::uint8_t>(id));
  if (db_.find_master_key(hex_id)) {
    id = crypto::generate_id();
    hex_id = store::bytes_to_hex(std::span<const std::uint8_t>(id));
  }

  crypto::Salt salt{};
  crypto::random_bytes(std::span<std::uint8_t>(salt));
  const crypto::Kek new_kek = crypto::derive_kek(new_password, salt);

  SealedKeys sealed;
  if (!seal_keys(new_kek, std::span<const std::uint8_t>(id),
                 std::span<const std::uint8_t>(pub_plain.data(), pub_plain.size()),
                 std::span<const std::uint8_t>(priv_plain.data(), priv_plain.size()),
                 &sealed)) {
    return Error::kInternal;
  }

  container::MasterKeyFile dst;
  dst.master_key_id = id;
  dst.name = src.name;
  dst.salt = salt;
  dst.kdf_mem = crypto::kKdfMemoryKiB;
  dst.kdf_iter = crypto::kKdfIterations;
  dst.kdf_par = crypto::kKdfParallelism;
  dst.pub_key_nonce = sealed.pub_nonce;
  dst.pub_key_tag = sealed.pub_tag;
  dst.pub_key_cipher = std::move(sealed.pub_cipher);
  dst.priv_key_nonce = sealed.priv_nonce;
  dst.priv_key_tag = sealed.priv_tag;
  dst.priv_key_cipher = std::move(sealed.priv_cipher);

  const std::string rel = store::FileStore::master_key_rel_path(hex_id);
  std::string io_err;
  if (!files_.write_atomic(rel, container::serialize(dst), &io_err)) {
    return Error::kImportSaveFailed;
  }

  store::MasterKeyRow row;
  row.master_key_id = hex_id;
  row.name = src.name;
  row.file_path = rel;
  row.mk_alg = container::kAlgRsa2048;
  row.is_default = false;  // §2.4：导入的密钥不自动成为默认主密钥
  const store::StoreError se = db_.upsert_master_key(row);
  if (se != store::StoreError::kOk) {
    files_.remove(rel, &io_err);
    return Error::kImportSaveFailed;
  }
  return Error::kOk;
}

Error MasterKeyService::switch_default(std::string_view new_default_id,
                                       std::span<const std::uint8_t> old_password,
                                       std::span<const std::uint8_t> new_password) {
  const std::optional<std::string> current = db_.default_master_key_id();
  if (!current) return Error::kMasterKeyNotFound;
  if (!db_.find_master_key(new_default_id)) return Error::kMasterKeyNotFound;

  const Error old_rc = verify_password(*current, old_password);
  if (old_rc != Error::kOk) return old_rc;

  // 两个密码相同时（同一人操作）只验证一次，避免要求用户重复输入。
  if (std::memcmp(old_password.data(), new_password.data(),
                  std::min(old_password.size(), new_password.size())) != 0 ||
      old_password.size() != new_password.size()) {
    const Error new_rc = verify_password(new_default_id, new_password);
    if (new_rc != Error::kOk) return new_rc;
  }

  const store::StoreError se = db_.set_default_master_key(new_default_id);
  return se == store::StoreError::kOk ? Error::kOk : Error::kInternal;
}

Error MasterKeyService::remove(std::string_view master_key_id,
                               std::span<const std::uint8_t> password) {
  const std::optional<store::MasterKeyRow> row = db_.find_master_key(master_key_id);
  if (!row) return Error::kMasterKeyNotFound;
  // §2.6：禁止删除默认主密钥。
  if (row->is_default || db_.default_master_key_id().value_or("") == row->master_key_id) {
    return Error::kCannotDeleteDefault;
  }

  const Error vr = verify_password(master_key_id, password);
  if (vr != Error::kOk) return vr;

  const store::StoreError se = db_.delete_master_key(master_key_id);
  if (se != store::StoreError::kOk) return Error::kInternal;

  // 索引条目删除后才删数据文件：顺序反过来会在删索引失败时留下孤儿文件。
  std::string io_err;
  files_.remove(row->file_path, &io_err);
  return Error::kOk;
}

}  // namespace secretkeeper::core
