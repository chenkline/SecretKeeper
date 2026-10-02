#include "secret_service.h"

#include <cstring>

#include "../store/hex.h"
#include "text.h"

namespace secretkeeper::core {
namespace {

constexpr std::size_t kCipherTail = crypto::kTagLength;

std::span<const std::uint8_t> cipher_body(std::span<const std::uint8_t> v) {
  return v.subspan(0, v.size() - kCipherTail);
}

}  // namespace

std::vector<SecretListItem> SecretService::list() const {
  std::vector<SecretListItem> out;
  for (const store::SecretRow& row : db_.list_secrets()) {
    SecretListItem item;
    item.secret_id = row.secret_id;
    item.master_key_id = row.master_key_id;
    item.title = row.title;

    // A dangling reference is legal: the master key may have been deleted while
    // its secrets were kept (requirement 2.6). The UI turns this into a yellow
    // question mark on the ID column.
    if (const std::optional<store::MasterKeyRow> mk = db_.find_master_key(row.master_key_id)) {
      item.master_key_found = true;
      item.master_key_name = mk->name;  // may legitimately be empty
    } else {
      item.master_key_found = false;
      item.master_key_name.clear();
    }
    out.push_back(std::move(item));
  }
  return out;
}

Error SecretService::load_file(std::string_view secret_id, container::SecretFile* out) const {
  const std::optional<store::SecretRow> row = db_.find_secret(secret_id);
  if (!row) return Error::kSecretNotFound;

  std::vector<std::uint8_t> bytes;
  std::string io_err;
  if (!files_.read(row->file_path, &bytes, &io_err)) return Error::kIoError;

  container::ParseError perr = container::ParseError::kOk;
  auto parsed = container::parse_secret(bytes, &perr);
  if (!parsed) {
    switch (perr) {
      case container::ParseError::kUnsupportedVersion: return Error::kImportVersionTooHigh;
      default: return Error::kImportBadFormat;
    }
  }
  *out = std::move(*parsed);
  return Error::kOk;
}

Error SecretService::add(std::string_view title, std::string_view plaintext,
                         std::string_view master_key_id, const crypto::Kek& master_kek,
                         std::string* out_secret_id) {
  // 3.2 length validation is by Unicode code point, not byte length, and comes
  // FIRST: the requirement says to reject and prompt immediately, before asking
  // the user to pick a key or enter a password.
  if (plaintext.empty()) return Error::kEmptySecret;
  if (text::count_code_points(plaintext) > text::kMaxSecretLength) return Error::kSecretTooLong;
  if (db_.secret_count() >= store::kMaxSecrets) return Error::kSecretQuotaExceeded;
  if (!crypto::cng_has_asymmetric_support()) return Error::kCryptoUnsupported;
  if (!db_.find_master_key(master_key_id)) return Error::kMasterKeyNotFound;

  crypto::SecureBytes pub_der;
  const Error ke = keys_.public_key_der(master_key_id, master_kek, &pub_der);
  if (ke != Error::kOk) return ke;

  // 3.2 step 3: a fresh AES-256 data key per secret.
  crypto::DataKey dk{};
  crypto::random_bytes(std::span<std::uint8_t>(dk));

  const crypto::Id new_secret_id = crypto::generate_id();

  // 3.2 step 5: wrap the data key with the master key PUBLIC key.
  const auto wrapped = crypto::rsa_oaep_encrypt(
      std::span<const std::uint8_t>(pub_der.data(), pub_der.size()),
      std::span<const std::uint8_t>(dk));
  if (!wrapped) return Error::kInternal;

  crypto::Nonce nonce{};
  crypto::random_bytes(std::span<std::uint8_t>(nonce));

  // 3.2 step 4: encrypt with the data key. AAD is the raw secretId.
  std::vector<std::uint8_t> cipher(plaintext.size() + kCipherTail, 0);
  crypto::Tag tag{};
  const std::span<const std::uint8_t> aad(new_secret_id);
  // plaintext is a string_view; reinterpret as bytes rather than going through
  // a temporary std::string.
  const std::span<const std::uint8_t> plain_span{
      reinterpret_cast<const std::uint8_t*>(plaintext.data()), plaintext.size()};
  crypto::aes_gcm_encrypt(dk.data(), dk.size(), nonce, aad, plain_span, cipher.data(),
                          tag.data());

  store::Id id_check{};
  if (!store::id_from_hex(master_key_id, &id_check)) return Error::kMasterKeyNotFound;

  container::SecretFile f;
  f.secret_id = new_secret_id;
  f.master_key_id = id_check;
  f.title = std::string(title);
  f.wrapped_dk.assign(wrapped->begin(), wrapped->end());
  f.data_nonce = nonce;
  f.data_tag = tag;
  f.data_cipher = std::move(cipher);

  const std::string hex_id = store::bytes_to_hex(std::span<const std::uint8_t>(new_secret_id));
  const std::string rel = store::FileStore::secret_rel_path(hex_id);

  std::string io_err;
  if (!files_.write_atomic(rel, container::serialize(f), &io_err)) return Error::kIoError;

  store::SecretRow row;
  row.secret_id = hex_id;
  row.master_key_id = std::string(master_key_id);
  row.title = std::string(title);
  row.file_path = rel;
  const store::StoreError se = db_.upsert_secret(row);
  if (se != store::StoreError::kOk) {
    files_.remove(rel, &io_err);
    return se == store::StoreError::kQuotaExceededSecrets ? Error::kSecretQuotaExceeded
                                                          : Error::kIoError;
  }
  if (out_secret_id != nullptr) *out_secret_id = hex_id;
  return Error::kOk;
}

Error SecretService::plaintext_of(std::string_view secret_id, const crypto::Kek& master_kek,
                                  std::string* out) const {
  container::SecretFile f;
  const Error e = load_file(secret_id, &f);
  if (e != Error::kOk) return e;

  crypto::SecureBytes priv_der;
  const std::string mk_hex = store::bytes_to_hex(std::span<const std::uint8_t>(f.master_key_id));
  const Error ke = keys_.private_key_der(mk_hex, master_kek, &priv_der);
  if (ke != Error::kOk) return ke;

  // 3.4 step 3: unwrap the data key with the master key PRIVATE key.
  const auto dk = crypto::rsa_oaep_decrypt(
      std::span<const std::uint8_t>(priv_der.data(), priv_der.size()),
      std::span<const std::uint8_t>(f.wrapped_dk));
  if (!dk) return Error::kImportDecryptFailed;

  // 3.4 step 4: decrypt with the data key, AAD is the raw secretId.
  std::vector<std::uint8_t> plain(f.data_cipher.size() - kCipherTail, 0);
  if (crypto::aes_gcm_decrypt(dk->data(), dk->size(), f.data_nonce,
                              std::span<const std::uint8_t>(f.secret_id),
                              cipher_body(f.data_cipher), f.data_tag,
                              plain.data()) != crypto::CryptoError::kOk) {
    return Error::kImportDecryptFailed;
  }
  out->assign(reinterpret_cast<const char*>(plain.data()), plain.size());
  return Error::kOk;
}

Error SecretService::export_to(std::string_view secret_id, std::vector<std::uint8_t>* out) const {
  const std::optional<store::SecretRow> row = db_.find_secret(secret_id);
  if (!row) return Error::kSecretNotFound;
  // Requirement 3.3: export content is byte-identical to the data file.
  std::string io_err;
  if (!files_.read(row->file_path, out, &io_err)) return Error::kIoError;
  return Error::kOk;
}

Error SecretService::import_from(std::span<const std::uint8_t> export_bytes,
                                 const crypto::Kek& master_kek, std::string* out_secret_id) {
  container::ParseError perr = container::ParseError::kOk;
  auto parsed = container::parse_secret(export_bytes, &perr);
  if (!parsed) {
    switch (perr) {
      case container::ParseError::kUnsupportedVersion: return Error::kImportVersionTooHigh;
      default: return Error::kImportBadFormat;
    }
  }
  if (db_.secret_count() >= store::kMaxSecrets) return Error::kSecretQuotaExceeded;

  container::SecretFile src = std::move(*parsed);

  // 3.4 step 2: the master key must exist locally, otherwise we cannot unwrap.
  const std::string mk_hex = store::bytes_to_hex(std::span<const std::uint8_t>(src.master_key_id));
  if (!db_.find_master_key(mk_hex)) return Error::kImportMasterKeyMissing;

  // Decrypt inline rather than through plaintext_of(): the record is not in the
  // index yet, so there is no row to look up.
  crypto::SecureBytes priv_der;
  if (keys_.private_key_der(mk_hex, master_kek, &priv_der) != Error::kOk) {
    return Error::kPasswordWrong;
  }
  const auto dk = crypto::rsa_oaep_decrypt(
      std::span<const std::uint8_t>(priv_der.data(), priv_der.size()),
      std::span<const std::uint8_t>(src.wrapped_dk));
  if (!dk) return Error::kImportDecryptFailed;

  std::vector<std::uint8_t> plain(src.data_cipher.size() - kCipherTail, 0);
  if (crypto::aes_gcm_decrypt(dk->data(), dk->size(), src.data_nonce,
                              std::span<const std::uint8_t>(src.secret_id),
                              cipher_body(src.data_cipher), src.data_tag,
                              plain.data()) != crypto::CryptoError::kOk) {
    return Error::kImportDecryptFailed;
  }

  // docs/03-data 7.2: keep the original ID so an export/import round trip is
  // lossless; on collision generate a new one so nothing local is overwritten.
  crypto::Id id = src.secret_id;
  std::string hex_id = store::bytes_to_hex(std::span<const std::uint8_t>(id));
  if (db_.find_secret(hex_id)) {
    id = crypto::generate_id();
    hex_id = store::bytes_to_hex(std::span<const std::uint8_t>(id));
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

  const std::string rel = store::FileStore::secret_rel_path(hex_id);
  std::string io_err;
  if (!files_.write_atomic(rel, container::serialize(src), &io_err)) {
    return Error::kImportSaveFailed;
  }

  store::SecretRow row;
  row.secret_id = hex_id;
  row.master_key_id = mk_hex;
  row.title = src.title;
  row.file_path = rel;
  const store::StoreError se = db_.upsert_secret(row);
  if (se != store::StoreError::kOk) {
    files_.remove(rel, &io_err);
    return se == store::StoreError::kQuotaExceededSecrets ? Error::kSecretQuotaExceeded
                                                          : Error::kImportSaveFailed;
  }
  if (out_secret_id != nullptr) *out_secret_id = hex_id;
  return Error::kOk;
}

Error SecretService::detail(std::string_view secret_id, const crypto::Kek* master_kek,
                            SecretDetail* out) const {
  const std::optional<store::SecretRow> row = db_.find_secret(secret_id);
  if (!row) return Error::kSecretNotFound;

  SecretDetail d;
  d.secret_id = row->secret_id;
  d.master_key_id = row->master_key_id;
  d.title = row->title;

  const std::optional<store::MasterKeyRow> mk = db_.find_master_key(row->master_key_id);
  if (!mk) {
    // Requirement 3.5: missing master key => masked plaintext, empty name column,
    // and the UI appends a yellow question mark to the ID column.
    d.master_key_found = false;
    d.master_key_name.clear();
    d.plaintext_masked = true;
    *out = std::move(d);
    return Error::kOk;
  }

  // Master key exists but its name may legitimately be empty. That is NOT the
  // same as "not found" and must not produce a question mark.
  d.master_key_found = true;
  d.master_key_name = mk->name;

  if (master_kek == nullptr) {
    // Caller has no KEK (non-default key, password not supplied yet).
    d.plaintext_masked = true;
    *out = std::move(d);
    return Error::kOk;
  }

  std::string plaintext;
  const Error e = plaintext_of(secret_id, *master_kek, &plaintext);
  if (e != Error::kOk) return e;
  d.plaintext = std::move(plaintext);
  d.plaintext_masked = false;
  *out = std::move(d);
  return Error::kOk;
}

Error SecretService::remove(std::string_view secret_id) {
  const std::optional<store::SecretRow> row = db_.find_secret(secret_id);
  if (!row) return Error::kSecretNotFound;

  // Delete the index row first, then the data file. The reverse order would leave
  // an orphan file if the index delete failed.
  const store::StoreError se = db_.delete_secret(secret_id);
  if (se != store::StoreError::kOk) return Error::kInternal;

  std::string io_err;
  files_.remove(row->file_path, &io_err);
  return Error::kOk;
}

}  // namespace secretkeeper::core
