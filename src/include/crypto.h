#pragma once

// SecretKeeper - cryptography layer public interface.
//
// All five platforms keep the same names and semantics so implementations can be
// compared side by side. This header declares primitives only; it carries no
// business meaning. Every caller must pass zeroable buffers for key material.
//
// Windows and Linux both use mbedTLS (vendored, see vendor/mbedtls/README.md).
// The other three platforms may pick their own provider as long as they reproduce
// the golden vectors byte for byte -- mbedTLS is not mandated there.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace secretkeeper::crypto {

// ---- Fixed parameters (must match docs/02-crypto; any change requires
// regenerating test-vectors) ----
inline constexpr std::uint32_t kKdfMemoryKiB = 10240;
inline constexpr std::uint32_t kKdfIterations = 3;
inline constexpr std::uint32_t kKdfParallelism = 1;
inline constexpr std::uint32_t kKdfVersion = 19;
inline constexpr std::size_t kKekLength = 32;
inline constexpr std::size_t kSaltLength = 16;
inline constexpr std::size_t kDataKeyLength = 32;
inline constexpr std::size_t kNonceLength = 12;
inline constexpr std::size_t kTagLength = 16;
inline constexpr std::size_t kIdLength = 16;
inline constexpr std::size_t kRsaModulusBytes = 256;

using Kek = std::array<std::uint8_t, kKekLength>;
using Salt = std::array<std::uint8_t, kSaltLength>;
using DataKey = std::array<std::uint8_t, kDataKeyLength>;
using Nonce = std::array<std::uint8_t, kNonceLength>;
using Tag = std::array<std::uint8_t, kTagLength>;
using Id = std::array<std::uint8_t, kIdLength>;

// Zeroable byte buffer for key material; wiped on destruction.
class SecureBytes {
 public:
  SecureBytes() = default;
  explicit SecureBytes(std::size_t n) : data_(n, 0) {}
  SecureBytes(const std::uint8_t* p, std::size_t n) : data_(p, p + n) {}
  // Copying key material is banned: copies cannot be tracked, which breaks the
  // zeroisation guarantee.
  SecureBytes(const SecureBytes&) = delete;
  SecureBytes& operator=(const SecureBytes&) = delete;
  SecureBytes(SecureBytes&& o) noexcept : data_(std::move(o.data_)) {
    o.data_.clear();
  }
  SecureBytes& operator=(SecureBytes&& o) noexcept {
    if (this != &o) {
      clear();
      data_ = std::move(o.data_);
      o.data_.clear();
    }
    return *this;
  }

  std::uint8_t* data() noexcept { return data_.data(); }
  const std::uint8_t* data() const noexcept { return data_.data(); }
  std::size_t size() const noexcept { return data_.size(); }
  std::span<const std::uint8_t> span() const noexcept { return {data_.data(), data_.size()}; }

  ~SecureBytes() { clear(); }
  void clear() noexcept;

 private:
  std::vector<std::uint8_t> data_;
};

// ---- Error type: distinguishes auth failure from other failures; the UI maps
// every one of them onto the single requirement-mandated wording. ----
enum class CryptoError {
  kOk = 0,
  kAuthFailed,
  kInvalidInput,
  kUnsupportedAlgorithm,
  kInternal,
};

const char* to_string(CryptoError e);

// ---- Capability probe ----
// mbedTLS is pure software, so RSA is always available and this always returns
// true. It stays in the interface for one reason: the test suite branches on it,
// which lets the build assert that no RSA-dependent path is silently skipped.
bool has_asymmetric_support();

// ---- Randomness ----
// mbedTLS CTR-DRBG seeded from the platform entropy source. Never a
// non-cryptographic RNG.
void random_bytes(std::span<std::uint8_t> out);
Id generate_id();

// ---- Key derivation ----
// KEK = Argon2id(password, salt, m, t, p, outlen=32)
// The caller generates the salt and stores it with the record; that is what makes
// cross-platform consistency possible.
Kek derive_kek(std::span<const std::uint8_t> password, const Salt& salt);

// ---- AES-256-GCM ----
// Encrypting the public key and the private key MUST use different nonces:
// reusing one makes pubKeyCipher XOR privKeyCipher directly recoverable.
void aes_gcm_encrypt(const std::uint8_t* key, std::size_t key_len,
                     const Nonce& nonce,
                     std::span<const std::uint8_t> aad,
                     std::span<const std::uint8_t> plaintext,
                     std::uint8_t* cipher_out, std::uint8_t* tag_out);

CryptoError aes_gcm_decrypt(const std::uint8_t* key, std::size_t key_len,
                            const Nonce& nonce,
                            std::span<const std::uint8_t> aad,
                            std::span<const std::uint8_t> cipher,
                            const Tag& tag,
                            std::uint8_t* plaintext_out);

// ---- RSA-2048 ----
struct RsaKeyPair {
  SecureBytes public_der;   // PKCS#1 RSAPublicKey
  SecureBytes private_der;  // PKCS#8 PrivateKeyInfo
};

RsaKeyPair generate_rsa2048();

// Wrap a data key with the public key using RSA-OAEP, MGF1 hash and OAEP hash
// both explicitly SHA-256 (never rely on a library default). nullopt on failure.
std::optional<std::array<std::uint8_t, kRsaModulusBytes>> rsa_oaep_encrypt(
    std::span<const std::uint8_t> public_der, std::span<const std::uint8_t> data_key);

std::optional<DataKey> rsa_oaep_decrypt(
    std::span<const std::uint8_t> private_der,
    std::span<const std::uint8_t> wrapped);

}  // namespace secretkeeper::crypto
