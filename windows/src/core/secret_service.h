#pragma once

// SecretKeeper - secret business logic.
//
// Implements the six operations in docs/04-requirements 3: list, add, export,
// import, view detail, delete. This layer only orchestrates; all byte-level work
// is delegated to the crypto / container / store layers.
//
// Password rules baked in here:
//   - Adding a secret under a NON-default master key requires that key's password
//   - Viewing plaintext under a NON-default master key requires that key's password
//   - Deleting a secret whose master key is NOT the default requires that key's password
//
// The yellow question mark lives in the UI layer, not here. This layer only
// reports master_key_found so the UI can decide independently of the name column.

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "../container/container.h"
#include "../crypto/crypto.h"
#include "../store/file_store.h"
#include "../store/index_db.h"
#include "error.h"
#include "master_key_service.h"

namespace secretkeeper::core {

// One row of the secret list (requirement 3.1).
//
// master_key_found drives the yellow question mark appended to the master key ID
// column. It is deliberately independent of master_key_name: an empty name with
// a found key shows NO question mark, and an empty name with a missing key DOES.
struct SecretListItem {
  std::string secret_id;
  std::string master_key_id;
  std::string master_key_name;
  std::string title;
  bool master_key_found = false;
};

// Detail view (requirement 3.5). When the master key is missing the UI masks the
// plaintext with 6 asterisks; this layer returns plaintext_masked instead so the
// caller never has to invent the mask itself.
struct SecretDetail {
  std::string secret_id;
  std::string master_key_id;
  std::string master_key_name;
  std::string title;
  bool master_key_found = false;
  std::string plaintext;         // empty when masked
  bool plaintext_masked = false; // true => UI shows "******"
};

// Requirement 3.5: plaintext is replaced by exactly 6 asterisks when the master
// key cannot be found locally.
inline constexpr const char* kMaskedPlaintext = "******";

class SecretService {
 public:
  SecretService(store::IndexDb& db, store::FileStore& files, MasterKeyService& keys)
      : db_(db), files_(files), keys_(keys) {}

  // 3.1 List secrets. Does not decrypt anything.
  std::vector<SecretListItem> list() const;

  // 3.2 Add a secret.
  //
  // kek_for_master must be the KEK of master_key_id. Passing an empty span when
  // master_key_id is not the default is a programming error and is rejected.
  Error add(std::string_view title, std::string_view plaintext,
            std::string_view master_key_id, const crypto::Kek& master_kek,
            std::string* out_secret_id);

  // 3.3 Export: returns the data file bytes verbatim (requirement: byte-identical).
  Error export_to(std::string_view secret_id, std::vector<std::uint8_t>* out) const;

  // 3.4 Import. Requires the KEK of the record's master key.
  Error import_from(std::span<const std::uint8_t> export_bytes, const crypto::Kek& master_kek,
                    std::string* out_secret_id);

  // 3.5 View detail. kek may be empty when the master key is the default one and
  // the caller expects the KEK cache to supply it instead.
  Error detail(std::string_view secret_id, const crypto::Kek* master_kek,
               SecretDetail* out) const;

  // 3.6 Delete.
  Error remove(std::string_view secret_id);

  // Decrypt a secret's plaintext. Shared by detail().
  Error plaintext_of(std::string_view secret_id, const crypto::Kek& master_kek,
                     std::string* out) const;

  // Reads and parses the secret data file.
  Error load_file(std::string_view secret_id, container::SecretFile* out) const;

 private:
  store::IndexDb& db_;
  store::FileStore& files_;
  MasterKeyService& keys_;
};

}  // namespace secretkeeper::core
