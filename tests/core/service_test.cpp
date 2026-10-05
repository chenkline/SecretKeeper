// SecretKeeper - service layer end-to-end self-check.
//
// Drives the public Service facade only: no store, no serialize, no crypto.
// Every requirement flow is exercised through the same entry points the UI uses,
// including the optional-password convention (empty password = "use the cached
// default KEK", non-default target without a password = kNeedsPassword).
//
// mbedTLS is pure software, so every RSA-dependent flow runs on every host.
//
// Test output must stay pure ASCII.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "core/service.h"

namespace svc = secretkeeper::service;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL  %s\n", what.c_str());
  }
}

std::span<const std::uint8_t> pwd(const char* s) {
  return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s),
                                       std::strlen(s));
}

const svc::Error kOk = svc::Error::kOk;

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "secretkeeper_service_test";
  std::error_code ec;
  std::filesystem::remove_all(tmp, ec);

  std::printf("Service layer self-check\n");

  svc::Service svc_;
  check(svc_.open((tmp / "data").string()) == kOk, "service opens the data directory");
  check(svc_.is_open(), "service reports open");
  check(!svc_.is_unlocked(), "starts locked");

  // ---- RSA-independent paths ----
  std::printf("Listing, lookup and validation gates\n");
  check(svc_.list_master_keys().empty(), "no master keys initially");
  check(svc_.list_secrets().empty(), "no secrets initially");

  const std::string missing = "00000000000000000000000000000000";
  check(svc_.verify_master_key_password(missing, pwd("x")) == svc::Error::kMasterKeyNotFound,
        "verify on missing key reports not found");
  check(svc_.delete_master_key(missing, pwd("x")) == svc::Error::kMasterKeyNotFound,
        "delete missing key reports not found");
  check(svc_.get_secret_detail(missing, pwd("x"), nullptr) == svc::Error::kSecretNotFound,
        "detail on missing secret reports not found");

  // An unknown master key is rejected before any password work.
  check(svc_.add_secret("t", "hello", missing, pwd("x")) == svc::Error::kMasterKeyNotFound,
        "add under unknown master key rejected");

  // Length validation happens BEFORE the key lookup and the quota, per 3.2.
  check(svc_.add_secret("t", "", missing, {}) == svc::Error::kEmptySecret,
        "empty plaintext rejected before anything else");
  check(svc_.add_secret("t", std::string(151, 'a'), missing, {}) == svc::Error::kSecretTooLong,
        "151 ascii chars rejected as too long");

  // 150 code points of 3-byte CJK = 450 bytes. A byte-length check would wrongly
  // reject this; a code-point check must not.
  std::string cjk;
  for (int i = 0; i < 150; ++i) cjk += "\xe4\xb8\xad";
  check(cjk.size() == 450, "150 CJK chars is 450 bytes");
  check(svc_.add_secret("t", cjk, missing, {}) == svc::Error::kMasterKeyNotFound,
        "150 CJK chars passes the length gate (byte length would have failed)");
  check(svc_.add_secret("t", cjk + "\xe4\xb8\xad", missing, {}) == svc::Error::kSecretTooLong,
        "151 CJK chars rejected as too long");
  check(svc::Service::secret_char_count(cjk) == 150, "char counter agrees with the gate");

  // ---- RSA-dependent paths ----
  std::printf("Master key create and default\n");
  const std::string pw1 = "correct horse battery staple";
  check(svc_.create_master_key("my key", pwd(pw1.c_str())) == kOk, "create first key");
  check(svc_.list_master_keys().size() == 1, "one master key listed");

  const std::string key_id = svc_.list_master_keys().front().master_key_id;
  check(svc_.list_master_keys().front().is_default, "first key is default");
  check(svc_.list_master_keys().front().name == "my key", "name round-trips");
  check(svc_.default_master_key_id().value_or("") == key_id, "default id reported");

  // Quota snapshot.
  svc::QuotaStatus q = svc_.quota();
  check(q.master_keys == 1 && q.max_master_keys == svc::kMaxMasterKeys, "quota reports keys");
  check(q.max_secrets == svc::kMaxSecrets, "quota reports the secret limit");

  // Wrong password must be reported as kPasswordWrong.
  check(svc_.verify_master_key_password(key_id, pwd("wrong")) == svc::Error::kPasswordWrong,
        "wrong password detected");
  check(svc_.verify_master_key_password(key_id, pwd(pw1.c_str())) == kOk,
        "correct password accepted");

  // Second key, not default.
  check(svc_.create_master_key("", pwd(pw1.c_str())) == kOk, "create second key");
  check(svc_.list_master_keys().size() == 2, "two master keys");
  const std::string key2_id = svc_.list_master_keys()[1].master_key_id;
  check(svc_.list_master_keys()[1].name.empty(), "second key has an empty name");
  check(!svc_.list_master_keys()[1].is_default, "second key is not default");

  // Quota: a third key must be refused.
  check(svc_.create_master_key("third", pwd(pw1.c_str())) == svc::Error::kMasterKeyQuotaExceeded,
        "third master key refused by quota");

  // Cannot delete the default key.
  check(svc_.delete_master_key(key_id, pwd(pw1.c_str())) == svc::Error::kCannotDeleteDefault,
        "default master key cannot be deleted");

  // ---- unlock / lock ----
  std::printf("Session and backoff\n");
  check(svc_.unlock(key_id, pwd("wrong")) == svc::Error::kPasswordWrong, "unlock rejects a wrong password");
  check(svc_.backoff().consecutive_failures >= 1, "failed unlock counts toward backoff");
  check(!svc_.is_unlocked(), "still locked after a failed unlock");
  check(svc_.unlock(key_id, pwd(pw1.c_str())) == kOk, "unlock succeeds");
  check(svc_.is_unlocked(), "unlocked");
  check(svc_.backoff().consecutive_failures == 0, "successful unlock clears the backoff");
  check(svc_.rotate_kek() == kOk, "kek rotation runs while unlocked");
  svc_.notify_activity();

  // ---- optional-password convention ----
  std::printf("Optional-password convention\n");
  // Default key + empty password -> uses the cached KEK, no prompt needed.
  check(svc_.export_master_key(key_id, pwd("prot"), nullptr) == svc::Error::kInternal,
        "export rejects a null output pointer");
  std::vector<std::uint8_t> mk_export;
  check(svc_.export_master_key(key_id, {}, &mk_export) == svc::Error::kPasswordWrong,
        "export requires a protection password");
  // Non-default key + empty password -> the UI must prompt.
  check(svc_.delete_master_key(key2_id, {}) == svc::Error::kNeedsPassword,
        "deleting a non-default key without a password asks for one");
  check(svc_.delete_master_key(key2_id, pwd("nope")) == svc::Error::kPasswordWrong,
        "delete with a wrong password refused");

  // ---- secrets ----
  std::printf("Secret add and detail\n");
  check(svc_.add_secret("", "top secret", key_id, {}) == kOk,
        "add secret with an empty title, default key, no password");
  check(svc_.add_secret("my title", "another", key_id, {}) == kOk, "add secret with a title");
  check(svc_.list_secrets().size() == 2, "two secrets listed");
  check(svc_.quota().secrets == 2, "quota counts secrets");

  const std::string secret_id = svc_.list_secrets().front().secret_id;
  // A non-default key with no password must ask for one.
  check(svc_.add_secret("t", "third", key2_id, {}) == svc::Error::kNeedsPassword,
        "adding under a non-default key without a password asks for one");
  check(svc_.add_secret("t", "third", key2_id, pwd(pw1.c_str())) == kOk,
        "adding under a non-default key with a password works");

  // Delete the non-default key; its secrets must survive as dangling references.
  check(svc_.delete_master_key(key2_id, pwd(pw1.c_str())) == kOk, "delete non-default key");

  const auto items = svc_.list_secrets();
  check(items.size() == 3, "secrets survive master key deletion");
  int dangling = 0;
  for (const auto& it : items) {
    if (!it.master_key_found) {
      ++dangling;
      check(it.master_key_name.empty(), "a dangling reference reports an empty name");
    } else {
      check(it.master_key_name == "my key", "master key name shown for the surviving key");
    }
  }
  check(dangling == 1, "exactly one secret reports a missing master key");

  // Detail with the default key: the cached KEK is used, no password needed.
  svc::SecretDetail det;
  check(svc_.get_secret_detail(secret_id, {}, &det) == kOk, "detail loads");
  check(!det.plaintext_masked, "plaintext is not masked for the default key");
  check(det.plaintext == "top secret", "plaintext round-trips");
  check(det.master_key_found, "master key found");

  std::string revealed;
  check(svc_.reveal_secret_plaintext(secret_id, {}, &revealed) == kOk, "reveal with no password");
  check(revealed == "top secret", "revealed plaintext matches");

  // ---- export / import round trip ----
  std::printf("Export and import\n");
  std::vector<std::uint8_t> exported;
  check(svc_.export_secret(secret_id, {}, &exported) == kOk, "export succeeds");
  check(!exported.empty(), "export produced bytes");

  // Import back: the ID collides, so a new ID is issued and the body re-encrypted.
  const std::size_t before = svc_.list_secrets().size();
  check(svc_.import_secret(exported, {}, {}) == kOk, "import succeeds");
  check(svc_.list_secrets().size() == before + 1, "import adds one record");

  std::string recovered;
  bool found_recovery = false;
  for (const auto& it : svc_.list_secrets()) {
    if (it.secret_id == secret_id) continue;
    if (svc_.reveal_secret_plaintext(it.secret_id, {}, &recovered) == kOk &&
        recovered == "top secret") {
      found_recovery = true;
      break;
    }
  }
  check(found_recovery, "the imported copy decrypts to the same plaintext");

  // Corrupt bytes must be rejected by the format parser.
  std::vector<std::uint8_t> corrupt = exported;
  corrupt[0] ^= 0xFF;  // break the magic
  check(svc_.import_secret(corrupt, {}, {}) == svc::Error::kImportBadFormat,
        "corrupt magic rejected on import");

  // ---- quota on secrets ----
  while (svc_.quota().secrets < svc::kMaxSecrets) {
    if (svc_.add_secret("filler", "x", key_id, {}) != kOk) break;
  }
  check(svc_.quota().secrets == svc::kMaxSecrets, "secrets reach the limit");
  check(svc_.add_secret("over", "x", key_id, {}) == svc::Error::kSecretQuotaExceeded,
        "one secret past the limit is refused");

  // ---- lock ----
  std::printf("Lock\n");
  svc_.lock();
  check(!svc_.is_unlocked(), "lock clears the session");
  check(svc_.export_secret(secret_id, {}, nullptr) == svc::Error::kInternal,
        "null output still rejected after lock");
  check(svc_.rotate_kek() == svc::Error::kLocked, "rotation refused while locked");

  // Deleting the default key is still refused after lock.
  check(svc_.delete_master_key(key_id, pwd(pw1.c_str())) == svc::Error::kCannotDeleteDefault,
        "default key still protected after lock");

  svc_.close();
  check(!svc_.is_open(), "close releases the database");

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
