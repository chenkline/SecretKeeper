#include "core/service.h"

#include <algorithm>
#include <cstring>

#include "core/_error.h"
#include "core/backoff.h"
#include "core/crypto.h"
#include "core/kek_cache.h"
#include "core/serialize.h"
#include "core/store.h"
#include "core/_text.h"

namespace secretkeeper::service {
namespace {


constexpr std::size_t kCipherTail = crypto::kTagLength;

std::span<const std::uint8_t> cipher_body(std::span<const std::uint8_t> v) {
  return v.subspan(0, v.size() - kCipherTail);
}

// AES-GCM 是流模式：密文长度 == 明文长度。存的是（明文 || tag），
// 因此明文长度是 (size - tag)。按密文总长分配输出会在恢复出的 DER
// 尾部多出 16 个零字节，RSA 解析器直接拒绝。
std::size_t plain_size(std::span<const std::uint8_t> cipher) {
  return cipher.size() - kCipherTail;
}

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

Error map_save(store::SaveError e) {
  switch (e) {
    case store::SaveError::kOk: return Error::kOk;
    case store::SaveError::kNotFound: return Error::kMasterKeyNotFound;
    case store::SaveError::kParseError: return Error::kImportBadFormat;
    case store::SaveError::kIoError: return Error::kIoError;
    case store::SaveError::kInvalidArgument: return Error::kInternal;
    case store::SaveError::kInternal: return Error::kInternal;
  }
  return Error::kInternal;
}

}  // namespace

// ===========================================================================
// Impl
// ===========================================================================

struct Service::Impl {
  store::IndexDb db;
  store::FileStore files;
  store::MasterKeyStore mk_store;
  store::SecretStore secret_store;
  ::secretkeeper::core::KekCache kek_cache;
  ::secretkeeper::core::PasswordBackoff backoff;

  Impl() : mk_store(db, files), secret_store(db, files) {}

  bool open(const std::string& data_dir) {
    files.set_data_dir(data_dir);
    std::string layout_error;
    if (!files.ensure_layout(&layout_error)) return false;
    return db.open(data_dir + "/secret.db") == store::StoreError::kOk;
  }

  // 取默认主密钥的缓存 KEK。未解锁返回 nullopt。
  std::optional<crypto::Kek> cached_kek() const { return kek_cache.peek(); }

  bool is_default(std::string_view master_key_id) const {
    return db.default_master_key_id().value_or("") == master_key_id;
  }

  // 记录一次密码失败并返回应提示的错误。密码错误才计入退避。
  Error note_password_failure() {
    // 首次失败就报「密码错误」；只有已经在退避窗口内再次尝试失败时
    // 才升级为「请稍后重试」，否则用户永远看不到真正的原因。
    const bool was_waiting = backoff.is_waiting();
    backoff.record_failure();
    return was_waiting ? Error::kRateLimited : Error::kPasswordWrong;
  }

  void note_password_success() { backoff.reset(); }

  // 解析本次操作该用哪个 KEK。
  //   密码非空 -> 校验该密码，成功则用它的 KEK
  //              （失败按密码错误计入退避，返回 kPasswordWrong 或 kRateLimited）
  //   密码为空 -> 仅限「目标是默认主密钥」且「已解锁」时用缓存 KEK；
  //              否则返回 kNeedsPassword，由 UI 弹框后带密码重试同一调用。
  Error resolve_kek(std::string_view master_key_id,
                    std::span<const std::uint8_t> password, crypto::Kek* out);
};

Error Service::Impl::resolve_kek(std::string_view master_key_id,
                                 std::span<const std::uint8_t> password,
                                 crypto::Kek* out) {
  if (!db.find_master_key(master_key_id)) return Error::kMasterKeyNotFound;

  if (password.empty()) {
    if (!is_default(master_key_id)) return Error::kNeedsPassword;
    const std::optional<crypto::Kek> cached = cached_kek();
    if (!cached.has_value()) return Error::kLocked;
    *out = *cached;
    return Error::kOk;
  }

  serialize::MasterKeyFile f;
  const store::SaveError se = mk_store.load(master_key_id, &f);
  if (se != store::SaveError::kOk) return map_save(se);

  const crypto::Kek kek = crypto::derive_kek(password, f.salt);
  crypto::SecureBytes probe(plain_size(f.pub_key_cipher));
  if (crypto::aes_gcm_decrypt(kek.data(), kek.size(), f.pub_key_nonce,
                              std::span<const std::uint8_t>(f.master_key_id),
                              cipher_body(f.pub_key_cipher), f.pub_key_tag,
                              probe.data()) != crypto::CryptoError::kOk) {
    return note_password_failure();
  }
  note_password_success();
  *out = kek;
  return Error::kOk;
}

// ===========================================================================
// 生命周期
// ===========================================================================

Service::Service() : impl_(new Impl()) {}
Service::~Service() { delete impl_; }

Error Service::open(const std::string& data_dir) {
  if (!impl_->open(data_dir)) return Error::kIoError;
  return Error::kOk;
}

void Service::close() {
  lock();
  impl_->db.close();
}

bool Service::is_open() const { return impl_->db.is_open(); }

// ===========================================================================
// 会话与锁定
// ===========================================================================

Error Service::unlock(std::string_view master_key_id,
                      std::span<const std::uint8_t> password) {
  if (!impl_->db.find_master_key(master_key_id)) return Error::kMasterKeyNotFound;

  serialize::MasterKeyFile f;
  const store::SaveError se = impl_->mk_store.load(master_key_id, &f);
  if (se != store::SaveError::kOk) return map_save(se);

  const crypto::Kek kek = crypto::derive_kek(password, f.salt);

  // 只解公钥即可判定密码正确性，不必付出解私钥的代价。
  crypto::SecureBytes probe(plain_size(f.pub_key_cipher));
  if (crypto::aes_gcm_decrypt(kek.data(), kek.size(), f.pub_key_nonce,
                              std::span<const std::uint8_t>(f.master_key_id),
                              cipher_body(f.pub_key_cipher), f.pub_key_tag,
                              probe.data()) != crypto::CryptoError::kOk) {
    return impl_->note_password_failure();
  }

  if (!impl_->kek_cache.load(kek)) return Error::kInternal;
  impl_->note_password_success();
  // 需求 4.2：每次解锁成功后立即轮换一次。
  impl_->kek_cache.rotate();
  return Error::kOk;
}

void Service::lock() {
  impl_->kek_cache.clear();
  impl_->backoff.reset();
}

bool Service::is_unlocked() const { return impl_->kek_cache.has_value(); }

Error Service::rotate_kek() {
  if (!is_unlocked()) return Error::kLocked;
  return impl_->kek_cache.rotate().has_value() ? Error::kOk : Error::kInternal;
}

void Service::notify_activity() {}

BackoffStatus Service::backoff() const {
  BackoffStatus s;
  s.waiting = impl_->backoff.is_waiting();
  s.remaining_seconds = impl_->backoff.remaining_seconds();
  s.consecutive_failures = impl_->backoff.consecutive_failures();
  return s;
}

// ===========================================================================
// KEK 获取：所有需要 KEK 的操作的统一入口
// ===========================================================================

// ===========================================================================
// 主密钥（需求 §2）
// ===========================================================================

std::vector<MasterKeyListItem> Service::list_master_keys() const {
  std::vector<MasterKeyListItem> out;
  for (const store::MasterKeyRow& row : impl_->db.list_master_keys()) {
    MasterKeyListItem item;
    item.master_key_id = row.master_key_id;
    item.name = row.name;
    item.is_default = row.is_default || impl_->is_default(row.master_key_id);
    item.secret_count = impl_->db.count_secrets_for_master_key(row.master_key_id);
    out.push_back(std::move(item));
  }
  return out;
}

Error Service::create_master_key(std::string_view name,
                                 std::span<const std::uint8_t> password) {
  if (password.empty()) return Error::kPasswordWrong;
  // 限额是业务规则，在 service 判定；存储层只如实计数。
  if (impl_->db.master_key_count() >= kMaxMasterKeys) {
    return Error::kMasterKeyQuotaExceeded;
  }
  if (!crypto::has_asymmetric_support()) return Error::kCryptoUnsupported;

  const crypto::Id id = crypto::generate_id();
  crypto::Salt salt{};
  crypto::random_bytes(std::span<std::uint8_t>(salt));
  const crypto::Kek kek = crypto::derive_kek(password, salt);

  crypto::RsaKeyPair pair = crypto::generate_rsa2048();

  SealedKeys sealed;
  if (!seal_keys(kek, std::span<const std::uint8_t>(id),
                 std::span<const std::uint8_t>(pair.public_der.data(),
                                                pair.public_der.size()),
                 std::span<const std::uint8_t>(pair.private_der.data(),
                                                pair.private_der.size()),
                 &sealed)) {
    return Error::kInternal;
  }

  serialize::MasterKeyFile f;
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

  // 第一把主密钥自动成为默认主密钥。
  const bool make_default = impl_->db.master_key_count() == 0;
  const store::SaveError se = impl_->mk_store.save(f, std::string(name), make_default);
  if (se != store::SaveError::kOk) return map_save(se);

  if (make_default) {
    impl_->db.set_default_master_key(
        store::bytes_to_hex(std::span<const std::uint8_t>(id)));
  }
  return Error::kOk;
}

Error Service::verify_master_key_password(std::string_view master_key_id,
                                          std::span<const std::uint8_t> password) {
  crypto::Kek kek{};
  const Error e = impl_->resolve_kek(master_key_id, password, &kek);
  return e;
}

Error Service::export_master_key(std::string_view master_key_id,
                                 std::span<const std::uint8_t> password,
                                 std::vector<std::uint8_t>* out) {
  if (out == nullptr) return Error::kInternal;
  if (password.empty()) {
    // 保护密码是导出时新设的，不能与主密钥密码混用，因此这里必须非空。
    return Error::kPasswordWrong;
  }

  crypto::Kek kek{};
  const Error ke = impl_->resolve_kek(master_key_id, password, &kek);
  if (ke != Error::kOk) return ke;

  serialize::MasterKeyFile f;
  const store::SaveError se = impl_->mk_store.load(master_key_id, &f);
  if (se != store::SaveError::kOk) return map_save(se);

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
  // masterKeyId 与 name 与本地一致；因保护密码不同，密文必然不同。
  crypto::Salt salt{};
  crypto::random_bytes(std::span<std::uint8_t>(salt));
  const crypto::Kek prot_kek = crypto::derive_kek(password, salt);

  SealedKeys sealed;
  if (!seal_keys(prot_kek, std::span<const std::uint8_t>(f.master_key_id),
                 std::span<const std::uint8_t>(pub_plain.data(), pub_plain.size()),
                 std::span<const std::uint8_t>(priv_plain.data(), priv_plain.size()),
                 &sealed)) {
    return Error::kInternal;
  }

  serialize::MasterKeyFile dst;
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

  *out = serialize::serialize(dst);
  return Error::kOk;
}

Error Service::import_master_key(std::span<const std::uint8_t> bytes,
                                 std::span<const std::uint8_t> protection_password,
                                 std::span<const std::uint8_t> new_password) {
  if (protection_password.empty() || new_password.empty()) return Error::kPasswordWrong;
  // 限额在 service 判定：导入会新增一把主密钥。
  if (impl_->db.master_key_count() >= kMaxMasterKeys) {
    return Error::kMasterKeyQuotaExceeded;
  }

  serialize::ParseError perr = serialize::ParseError::kOk;
  auto parsed = serialize::parse_master_key(bytes, &perr);
  if (!parsed) {
    switch (perr) {
      case serialize::ParseError::kUnsupportedVersion: return Error::kImportVersionTooHigh;
      case serialize::ParseError::kBadMagic:
      case serialize::ParseError::kTruncated:
      case serialize::ParseError::kLengthMismatch:
      case serialize::ParseError::kUnknownAlgorithm:
      case serialize::ParseError::kInvalidUtf8:
      default: return Error::kImportBadFormat;
    }
  }
  serialize::MasterKeyFile src = std::move(*parsed);

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
  if (impl_->db.find_master_key(hex_id)) {
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

  serialize::MasterKeyFile dst;
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

  // §2.4：导入的密钥不自动成为默认主密钥。
  const store::SaveError se =
      impl_->mk_store.save(dst, src.name, /*make_default=*/false);
  if (se != store::SaveError::kOk) return map_save(se);
  return Error::kOk;
}

Error Service::switch_default_master_key(std::string_view master_key_id,
                                         std::span<const std::uint8_t> password) {
  const std::optional<std::string> current = impl_->db.default_master_key_id();
  if (!current) return Error::kMasterKeyNotFound;
  if (!impl_->db.find_master_key(master_key_id)) return Error::kMasterKeyNotFound;

  // 需求 2.5：分别验证原默认与新默认两把密钥的密码，防止把密匣锁死。
  // 已解锁且未传密码时，两把密钥都能用同一个缓存 KEK 校验；
  // 若目标正是当前默认密钥，只需校验一次。
  if (*current != master_key_id) {
    crypto::Kek ignored{};
    const Error e = impl_->resolve_kek(master_key_id, password, &ignored);
    if (e != Error::kOk) return e;
  }

  const store::StoreError se = impl_->db.set_default_master_key(master_key_id);
  if (se != store::StoreError::kOk) return Error::kInternal;

  // 默认密钥换了，缓存里那把 KEK 不再对应任何默认密钥，必须清掉，
  // 否则后续「用默认 KEK」的操作会拿着旧密钥去解新默认密钥的文件。
  if (impl_->kek_cache.has_value()) impl_->kek_cache.clear();
  return Error::kOk;
}

Error Service::delete_master_key(std::string_view master_key_id,
                                 std::span<const std::uint8_t> password) {
  const std::optional<store::MasterKeyRow> row =
      impl_->db.find_master_key(master_key_id);
  if (!row) return Error::kMasterKeyNotFound;
  // §2.6：禁止删除默认主密钥。
  if (row->is_default || impl_->is_default(master_key_id)) {
    return Error::kCannotDeleteDefault;
  }

  crypto::Kek kek{};
  const Error ke = impl_->resolve_kek(master_key_id, password, &kek);
  if (ke != Error::kOk) return ke;

  const store::StoreError se = impl_->db.delete_master_key(master_key_id);
  if (se != store::StoreError::kOk) return Error::kInternal;

  // 索引条目删除后才删数据文件：顺序反过来会在删索引失败时留下孤儿文件。
  // 关联的机密信息**保留**（需求 2.6），其主密钥 ID 列随后显示黄色问号。
  std::string io_err;
  impl_->files.remove(row->file_path, &io_err);
  return Error::kOk;
}


// ===========================================================================
// 机密信息（需求 §3）
// ===========================================================================

std::vector<SecretListItem> Service::list_secrets() const {
  std::vector<SecretListItem> out;
  for (const store::SecretRow& row : impl_->db.list_secrets()) {
    SecretListItem item;
    item.secret_id = row.secret_id;
    item.master_key_id = row.master_key_id;
    item.title = row.title;

    // 悬空引用是合法状态：主密钥可能已删除而机密信息保留（需求 2.6）。
    // UI 据此在 ID 列后显示黄色问号——这与名称列是否为空无关。
    if (const std::optional<store::MasterKeyRow> mk =
            impl_->db.find_master_key(row.master_key_id)) {
      item.master_key_found = true;
      item.master_key_name = mk->name;  // 允许为空
    } else {
      item.master_key_found = false;
      item.master_key_name.clear();
    }
    out.push_back(std::move(item));
  }
  return out;
}

Error Service::add_secret(std::string_view title, std::string_view plaintext,
                          std::string_view master_key_id,
                          std::span<const std::uint8_t> master_password) {
  // 3.2 的长度校验按 Unicode 码点计，且**先于**选密钥与输密码：
  // 需求要求立即拒绝并提示，不该先让用户做无谓的选择。
  if (plaintext.empty()) return Error::kEmptySecret;
  if (secret_char_count(plaintext) > kMaxSecretChars) return Error::kSecretTooLong;
  if (impl_->db.secret_count() >= kMaxSecrets) return Error::kSecretQuotaExceeded;
  if (!crypto::has_asymmetric_support()) return Error::kCryptoUnsupported;

  crypto::Kek master_kek{};
  const Error ke = impl_->resolve_kek(master_key_id, master_password, &master_kek);
  if (ke != Error::kOk) return ke;

  serialize::MasterKeyFile mk_file;
  const store::SaveError kse = impl_->mk_store.load(master_key_id, &mk_file);
  if (kse != store::SaveError::kOk) return map_save(kse);

  crypto::SecureBytes pub_plain(plain_size(mk_file.pub_key_cipher));
  if (crypto::aes_gcm_decrypt(master_kek.data(), master_kek.size(), mk_file.pub_key_nonce,
                              std::span<const std::uint8_t>(mk_file.master_key_id),
                              cipher_body(mk_file.pub_key_cipher), mk_file.pub_key_tag,
                              pub_plain.data()) != crypto::CryptoError::kOk) {
    return Error::kPasswordWrong;
  }

  // 3.2 step 3: a fresh AES-256 data key per secret.
  crypto::DataKey dk{};
  crypto::random_bytes(std::span<std::uint8_t>(dk));

  const crypto::Id new_secret_id = crypto::generate_id();

  // 3.2 step 5: wrap the data key with the master key PUBLIC key.
  const auto wrapped = crypto::rsa_oaep_encrypt(
      std::span<const std::uint8_t>(pub_plain.data(), pub_plain.size()),
      std::span<const std::uint8_t>(dk));
  if (!wrapped) return Error::kInternal;

  crypto::Nonce nonce{};
  crypto::random_bytes(std::span<std::uint8_t>(nonce));

  // 3.2 step 4: encrypt with the data key. AAD is the raw secretId.
  std::vector<std::uint8_t> cipher(plaintext.size() + kCipherTail, 0);
  crypto::Tag tag{};
  const std::span<const std::uint8_t> aad(new_secret_id);
  const std::span<const std::uint8_t> plain_span{
      reinterpret_cast<const std::uint8_t*>(plaintext.data()), plaintext.size()};
  crypto::aes_gcm_encrypt(dk.data(), dk.size(), nonce, aad, plain_span, cipher.data(),
                          tag.data());

  store::Id mk_id{};
  if (!store::id_from_hex(master_key_id, &mk_id)) return Error::kMasterKeyNotFound;

  serialize::SecretFile f;
  f.secret_id = new_secret_id;
  f.master_key_id = mk_id;
  f.title = std::string(title);
  f.wrapped_dk.assign(wrapped->begin(), wrapped->end());
  f.data_nonce = nonce;
  f.data_tag = tag;
  f.data_cipher = std::move(cipher);

  const store::SaveError se = impl_->secret_store.save(f);
  if (se != store::SaveError::kOk) return map_save(se);
  return Error::kOk;
}

namespace {

// 解开数据密钥，再用它解密机密内容。这是详情、查看明文、导入三处的公共步骤。
Error unwrap_plaintext(store::MasterKeyStore& mk_store, const crypto::Kek& master_kek,
                       const serialize::SecretFile& f, std::vector<std::uint8_t>* out) {
  const std::string mk_hex =
      store::bytes_to_hex(std::span<const std::uint8_t>(f.master_key_id));

  serialize::MasterKeyFile mk;
  const store::SaveError se = mk_store.load(mk_hex, &mk);
  if (se != store::SaveError::kOk) {
    return se == store::SaveError::kNotFound ? Error::kMasterKeyNotFound
                                             : map_save(se);
  }

  crypto::SecureBytes priv_plain(plain_size(mk.priv_key_cipher));
  if (crypto::aes_gcm_decrypt(master_kek.data(), master_kek.size(), mk.priv_key_nonce,
                              std::span<const std::uint8_t>(mk.master_key_id),
                              cipher_body(mk.priv_key_cipher), mk.priv_key_tag,
                              priv_plain.data()) != crypto::CryptoError::kOk) {
    return Error::kPasswordWrong;
  }

  // 3.4 step 3: unwrap the data key with the master key PRIVATE key.
  const auto dk = crypto::rsa_oaep_decrypt(
      std::span<const std::uint8_t>(priv_plain.data(), priv_plain.size()),
      std::span<const std::uint8_t>(f.wrapped_dk));
  if (!dk) return Error::kImportDecryptFailed;

  // 3.4 step 4: decrypt with the data key, AAD is the raw secretId.
  out->assign(f.data_cipher.size() - kCipherTail, 0);
  if (crypto::aes_gcm_decrypt(dk->data(), dk->size(), f.data_nonce,
                              std::span<const std::uint8_t>(f.secret_id),
                              cipher_body(f.data_cipher), f.data_tag,
                              out->data()) != crypto::CryptoError::kOk) {
    return Error::kImportDecryptFailed;
  }
  return Error::kOk;
}

}  // namespace

Error Service::reveal_secret_plaintext(std::string_view secret_id,
                                       std::span<const std::uint8_t> master_password,
                                       std::string* out) {
  const std::optional<store::SecretRow> row = impl_->db.find_secret(secret_id);
  if (!row) return Error::kSecretNotFound;

  serialize::SecretFile f;
  const store::SaveError se = impl_->secret_store.load(secret_id, &f);
  if (se != store::SaveError::kOk) return map_save(se);

  crypto::Kek master_kek{};
  const Error ke = impl_->resolve_kek(row->master_key_id, master_password, &master_kek);
  if (ke != Error::kOk) return ke;

  std::vector<std::uint8_t> plain;
  const Error pe = unwrap_plaintext(impl_->mk_store, master_kek, f, &plain);
  if (pe != Error::kOk) return pe;

  out->assign(reinterpret_cast<const char*>(plain.data()), plain.size());
  return Error::kOk;
}

Error Service::get_secret_detail(std::string_view secret_id,
                                 std::span<const std::uint8_t> master_password,
                                 SecretDetail* out) {
  const std::optional<store::SecretRow> row = impl_->db.find_secret(secret_id);
  if (!row) return Error::kSecretNotFound;

  SecretDetail d;
  d.secret_id = row->secret_id;
  d.master_key_id = row->master_key_id;
  d.title = row->title;

  const std::optional<store::MasterKeyRow> mk =
      impl_->db.find_master_key(row->master_key_id);
  if (!mk) {
    // 需求 3.5：主密钥缺失 -> 掩码正文 + 空名称列 + ID 列黄色问号。
    d.master_key_found = false;
    d.master_key_name.clear();
    d.plaintext_masked = true;
    *out = std::move(d);
    return Error::kOk;
  }

  // 主密钥存在但名称可能为空。那**不是**「未找到」，不得产生问号。
  d.master_key_found = true;
  d.master_key_name = mk->name;

  // 尚未提供密码时先给掩码态，而不是报错——这样 UI 可以先展示列表，
  // 用户点了「查看」再索要密码。
  if (master_password.empty() && !impl_->is_default(row->master_key_id)) {
    d.plaintext_masked = true;
    *out = std::move(d);
    return Error::kOk;
  }

  std::string plaintext;
  const Error e = reveal_secret_plaintext(secret_id, master_password, &plaintext);
  if (e != Error::kOk) {
    if (e == Error::kNeedsPassword || e == Error::kLocked) {
      d.plaintext_masked = true;
      *out = std::move(d);
      return Error::kOk;
    }
    return e;
  }
  d.plaintext = std::move(plaintext);
  d.plaintext_masked = false;
  *out = std::move(d);
  return Error::kOk;
}

Error Service::export_secret(std::string_view secret_id,
                             std::span<const std::uint8_t> master_password,
                             std::vector<std::uint8_t>* out) {
  if (out == nullptr) return Error::kInternal;
  const std::optional<store::SecretRow> row = impl_->db.find_secret(secret_id);
  if (!row) return Error::kSecretNotFound;

  // 需求 3.3：导出内容与数据文件逐字节相同，因此不需要密码——
  // 文件本身已用数据密钥加密，而数据密钥由主密钥封装。
  // 参数保留是为了将来 v1.0.0 的「导出需验证」策略预留同一入口。
  (void)master_password;

  serialize::SecretFile f;
  const store::SaveError se = impl_->secret_store.load(secret_id, &f);
  if (se != store::SaveError::kOk) return map_save(se);

  *out = serialize::serialize(f);
  return Error::kOk;
}

Error Service::import_secret(std::span<const std::uint8_t> bytes,
                             std::span<const std::uint8_t> file_password,
                             std::span<const std::uint8_t> master_password) {
  if (impl_->db.secret_count() >= kMaxSecrets) return Error::kSecretQuotaExceeded;
  (void)file_password;  // v0.0.1 的导出文件不再包一层保护密码（需求 3.3）

  serialize::ParseError perr = serialize::ParseError::kOk;
  auto parsed = serialize::parse_secret(bytes, &perr);
  if (!parsed) {
    switch (perr) {
      case serialize::ParseError::kUnsupportedVersion: return Error::kImportVersionTooHigh;
      default: return Error::kImportBadFormat;
    }
  }
  serialize::SecretFile src = std::move(*parsed);

  // 3.4 step 2: the master key must exist locally, otherwise we cannot unwrap.
  const std::string mk_hex =
      store::bytes_to_hex(std::span<const std::uint8_t>(src.master_key_id));
  if (!impl_->db.find_master_key(mk_hex)) return Error::kImportMasterKeyMissing;

  crypto::Kek master_kek{};
  const Error ke = impl_->resolve_kek(mk_hex, master_password, &master_kek);
  if (ke != Error::kOk) return ke;

  // 解密校验：数据密钥必须真的能解开，否则文件已损坏或密码不对。
  std::vector<std::uint8_t> plain;
  const Error pe = unwrap_plaintext(impl_->mk_store, master_kek, src, &plain);
  if (pe != Error::kOk) return pe;

  // docs/03-data 7.2: keep the original ID so an export/import round trip is
  // lossless; on collision generate a new one so nothing local is overwritten.
  if (impl_->db.find_secret(
          store::bytes_to_hex(std::span<const std::uint8_t>(src.secret_id)))) {
    const crypto::Id id = crypto::generate_id();

    serialize::MasterKeyFile mk;
    const store::SaveError mse = impl_->mk_store.load(mk_hex, &mk);
    if (mse != store::SaveError::kOk) return map_save(mse);
    crypto::SecureBytes priv_plain(plain_size(mk.priv_key_cipher));
    if (crypto::aes_gcm_decrypt(master_kek.data(), master_kek.size(), mk.priv_key_nonce,
                                std::span<const std::uint8_t>(mk.master_key_id),
                                cipher_body(mk.priv_key_cipher), mk.priv_key_tag,
                                priv_plain.data()) != crypto::CryptoError::kOk) {
      return Error::kPasswordWrong;
    }
    const auto dk = crypto::rsa_oaep_decrypt(
        std::span<const std::uint8_t>(priv_plain.data(), priv_plain.size()),
        std::span<const std::uint8_t>(src.wrapped_dk));
    if (!dk) return Error::kImportDecryptFailed;

    // The ID is the GCM AAD, so a new ID means the ciphertext must be re-encrypted.
    crypto::Nonce nonce{};
    crypto::random_bytes(std::span<std::uint8_t>(nonce));
    std::vector<std::uint8_t> cipher(plain.size() + kCipherTail, 0);
    crypto::Tag tag{};
    crypto::aes_gcm_encrypt(dk->data(), dk->size(), nonce,
                            std::span<const std::uint8_t>(id), plain, cipher.data(),
                            tag.data());
    src.secret_id = id;
    src.data_nonce = nonce;
    src.data_tag = tag;
    src.data_cipher = std::move(cipher);
  }

  const store::SaveError se = impl_->secret_store.save(src);
  if (se != store::SaveError::kOk) return map_save(se);
  return Error::kOk;
}

Error Service::delete_secret(std::string_view secret_id,
                             std::span<const std::uint8_t> master_password) {
  const std::optional<store::SecretRow> row = impl_->db.find_secret(secret_id);
  if (!row) return Error::kSecretNotFound;

  // 需求 3.6：非默认主密钥的机密信息，删除需验证该密钥密码。
  crypto::Kek kek{};
  const Error ke = impl_->resolve_kek(row->master_key_id, master_password, &kek);
  if (ke != Error::kOk && ke != Error::kMasterKeyNotFound) return ke;

  // 先删索引行再删数据文件：反过来会在删索引失败时留下孤儿文件。
  const store::StoreError se = impl_->db.delete_secret(secret_id);
  if (se != store::StoreError::kOk) return Error::kInternal;

  std::string io_err;
  impl_->files.remove(row->file_path, &io_err);
  return Error::kOk;
}

// ===========================================================================
// 杂项
// ===========================================================================

QuotaStatus Service::quota() const {
  QuotaStatus s;
  s.master_keys = impl_->db.master_key_count();
  s.max_master_keys = kMaxMasterKeys;
  s.secrets = impl_->db.secret_count();
  s.max_secrets = kMaxSecrets;
  return s;
}

std::optional<std::string> Service::default_master_key_id() const {
  return impl_->db.default_master_key_id();
}

std::size_t Service::secret_char_count(std::string_view plaintext) {
  return text::count_code_points(plaintext);
}

}  // namespace secretkeeper::service
