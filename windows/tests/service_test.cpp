// SecretKeeper - service layer end-to-end self-check.
//
// Covers the main key and secret business flows against a real SQLite index and
// real data files. Every test that needs RSA is skipped with an explicit notice
// when the host CNG lacks asymmetric algorithms, rather than silently passing.
//
// Test output must stay pure ASCII.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../src/container/container.h"
#include "../src/core/master_key_service.h"
#include "../src/core/secret_service.h"
#include "../src/store/file_store.h"
#include "../src/store/index_db.h"
#include "../src/crypto/crypto.h"

namespace core = secretkeeper::core;
namespace crypto = secretkeeper::crypto;
namespace container = secretkeeper::container;
namespace store = secretkeeper::store;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skipped = 0;

void check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL  %s\n", what.c_str());
  }
}

void skip(const std::string& what) {
  ++g_skipped;
  std::printf("  SKIP  %s\n", what.c_str());
}

std::span<const std::uint8_t> pwd(const char* s) {
  return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s),
                                       std::strlen(s));
}

}  // namespace

int main() {
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "secretkeeper_service_test";
  std::error_code ec;
  std::filesystem::remove_all(tmp, ec);
  std::filesystem::create_directories(tmp / "data", ec);

  std::printf("Service layer self-check\n");

  store::FileStore files((tmp / "data").string());
  std::string err;
  check(files.ensure_layout(&err), "data layout created");

  store::IndexDb db;
  check(db.open((tmp / "secret.db").string()) == store::StoreError::kOk, "index opened");

  core::MasterKeyService keys(db, files);
  core::SecretService secrets(db, files, keys);

  // ---- RSA-independent paths ----
  std::printf("Master key listing and lookup\n");
  check(keys.list().empty(), "no master keys initially");
  check(secrets.list().empty(), "no secrets initially");

  // Password verification against a missing record.
  check(keys.verify_password("00000000000000000000000000000000", pwd("x")) ==
            core::Error::kMasterKeyNotFound,
        "verify on missing key reports not found");

  // Deleting a missing key.
  check(keys.remove("00000000000000000000000000000000", pwd("x")) ==
            core::Error::kMasterKeyNotFound,
        "remove missing key reports not found");

  // Adding with an unknown master key.
  //
  // On a host without RSA the service rejects with kCryptoUnsupported before it
  // ever reaches the key lookup, so the expected error depends on the host.
  const bool has_rsa = crypto::cng_has_asymmetric_support();
  const core::Error kEnvGate = has_rsa ? core::Error::kMasterKeyNotFound
                                       : core::Error::kCryptoUnsupported;

  std::string new_id;
  check(secrets.add("t", "hello", "00000000000000000000000000000000", crypto::Kek{}, &new_id) ==
            kEnvGate,
        "add under unknown master key rejected");

  // Length validation happens BEFORE the environment gate, so these two must
  // report length errors even on a host with no RSA support.
  crypto::Kek kek{};
  check(secrets.add("t", "", "00000000000000000000000000000000", kek, &new_id) ==
            core::Error::kEmptySecret,
        "empty plaintext rejected before anything else");
  check(secrets.add("t", std::string(151, 'a'), "00000000000000000000000000000000", kek,
                    &new_id) == core::Error::kSecretTooLong,
        "151 ascii chars rejected as too long");
  check(secrets.add("t", std::string(150, 'a'), "00000000000000000000000000000000", kek,
                    &new_id) == kEnvGate,
        "exactly 150 chars passes the length gate");

  // 150 code points of 3-byte CJK = 450 bytes. A byte-length check would wrongly
  // reject this; a code-point check accepts it.
  std::string cjk;
  for (int i = 0; i < 150; ++i) cjk += "\xe4\xb8\xad";
  check(cjk.size() == 450, "150 CJK chars is 450 bytes");
  check(secrets.add("t", cjk, "00000000000000000000000000000000", kek, &new_id) == kEnvGate,
        "150 CJK chars passes the length gate (byte length would have failed)");
  check(secrets.add("t", cjk + "\xe4\xb8\xad", "00000000000000000000000000000000", kek,
                    &new_id) == core::Error::kSecretTooLong,
        "151 CJK chars rejected as too long");

  // ---- RSA-dependent paths ----
  if (!crypto::cng_has_asymmetric_support()) {
    std::printf("\nRSA-dependent flows SKIPPED: this host CNG has no asymmetric\n"
                "algorithms. See AGENTS.md 9.4.\n");
    skip("master key create / secret add / export / import / detail / delete");
    db.close();
    std::printf("\n%d checks, %d failures, %d skipped\n", g_checks, g_failures, g_skipped);
    return g_failures == 0 ? 0 : 1;
  }

  std::printf("Master key create and default\n");
  const std::string pw1 = "correct horse battery staple";
  check(keys.create("my key", pwd(pw1.c_str()), true) == core::Error::kOk, "create first key");
  check(keys.list().size() == 1, "one master key listed");
  const std::string key_id = keys.list().front().master_key_id;
  check(keys.list().front().is_default, "first key is default");
  check(keys.list().front().name == "my key", "name round-trips");

  // Wrong password must be reported as kPasswordWrong.
  check(keys.verify_password(key_id, pwd("wrong")) == core::Error::kPasswordWrong,
        "wrong password detected");
  check(keys.verify_password(key_id, pwd(pw1.c_str())) == core::Error::kOk,
        "correct password accepted");

  // Second key, not default.
  check(keys.create("", pwd(pw1.c_str()), false) == core::Error::kOk, "create second key");
  check(keys.list().size() == 2, "two master keys");
  const std::string key2_id = keys.list()[1].master_key_id;
  check(keys.list()[1].name.empty(), "second key has an empty name");
  check(!keys.list()[1].is_default, "second key is not default");

  // Quota: a third key must be refused.
  check(keys.create("third", pwd(pw1.c_str()), false) == core::Error::kMasterKeyQuotaExceeded,
        "third master key refused by quota");

  // Cannot delete the default key.
  check(keys.remove(key_id, pwd(pw1.c_str())) == core::Error::kCannotDeleteDefault,
        "default master key cannot be deleted");

  // Delete the non-default key with the wrong password first.
  check(keys.remove(key2_id, pwd("nope")) == core::Error::kPasswordWrong,
        "delete with wrong password refused");

  std::printf("Secret add and detail\n");
  std::string secret_id;
  // The KEK must come from the record's own salt.
  container::MasterKeyFile mkf;
  check(keys.load_file(key_id, &mkf) == core::Error::kOk, "master key file loads");
  const crypto::Kek real_kek = crypto::derive_kek(pwd(pw1.c_str()), mkf.salt);

  check(secrets.add("", "top secret", key_id, real_kek, &secret_id) == core::Error::kOk,
        "add secret with empty title");
  check(secrets.add("my title", "another", key_id, real_kek, nullptr) == core::Error::kOk,
        "add secret with title");
  check(secrets.list().size() == 2, "two secrets listed");

  // A dangling reference must report master_key_found=false with an empty name.
  check(keys.remove(key2_id, pwd(pw1.c_str())) == core::Error::kOk, "delete non-default key");

  // List must not show a question-mark source for the surviving key.
  const auto items = secrets.list();
  check(items.size() == 2, "secrets survive master key deletion");
  for (const auto& it : items) {
    check(it.master_key_found, "secrets still resolve to the remaining key");
    check(it.master_key_name == "my key", "master key name shown");
  }

  // Detail: plaintext recovered with the correct KEK.
  core::SecretDetail det;
  check(secrets.detail(secret_id, &real_kek, &det) == core::Error::kOk, "detail loads");
  check(!det.plaintext_masked, "plaintext is not masked");
  check(det.plaintext == "top secret", "plaintext round-trips");
  check(det.master_key_found, "master key found");

  // Detail without a KEK must mask the plaintext.
  check(secrets.detail(secret_id, nullptr, &det) == core::Error::kOk, "detail without kek");
  check(det.plaintext_masked, "plaintext masked when no kek supplied");
  check(det.master_key_found, "master key still reported found while masked");

  // Export must be byte-identical to the data file.
  std::vector<std::uint8_t> exported;
  check(secrets.export_to(secret_id, &exported) == core::Error::kOk, "export succeeds");
  container::SecretFile parsed;
  container::ParseError perr = container::ParseError::kOk;
  auto pf = container::parse_secret(exported, &perr);
  check(pf.has_value(), "exported bytes parse as a secret file");
  if (pf) check(container::serialize(*pf) == exported, "export round-trips byte-identically");

  // Import back: ID collision means a new ID and re-encryption.
  std::string imported_id;
  check(secrets.import_from(exported, real_kek, &imported_id) == core::Error::kOk,
        "import succeeds");
  check(imported_id != secret_id, "import with colliding ID generates a new ID");
  std::string recovered;
  check(secrets.plaintext_of(imported_id, real_kek, &recovered) == core::Error::kOk,
        "imported secret decrypts");
  check(recovered == "top secret", "imported plaintext matches");

  // Wrong KEK on import.
  crypto::Kek wrong_kek{};
  wrong_kek[0] ^= 0xFF;
  std::string bad_id;
  check(secrets.import_from(exported, wrong_kek, &bad_id) != core::Error::kOk,
        "import with wrong kek rejected");

  // Corrupt bytes must be rejected by the format parser.
  std::vector<std::uint8_t> corrupt = exported;
  corrupt[0] ^= 0xFF;  // break the magic
  check(secrets.import_from(corrupt, real_kek, &bad_id) == core::Error::kImportBadFormat,
        "corrupt magic rejected on import");

  // Delete.
  check(secrets.remove(secret_id) == core::Error::kOk, "delete secret");
  check(secrets.remove(secret_id) == core::Error::kSecretNotFound, "delete twice reports not found");

  db.close();
  std::printf("\n%d checks, %d failures, %d skipped\n", g_checks, g_failures, g_skipped);
  return g_failures == 0 ? 0 : 1;
}
