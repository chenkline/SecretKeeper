// SecretKeeper - cryptography layer implementation (mbedTLS).
//
// mbedTLS supplies RSA-2048, AES-256-GCM, SHA-256 and a CTR-DRBG over the
// platform entropy source. Argon2id comes from the vendored Argon2 reference
// implementation (vendor/argon2, CC0/Apache-2.0).
//
// Why not CNG: Windows 11 build 22631 and the GitHub windows-2022 runner both
// ship a CNG with no asymmetric providers, so RSA always failed there. mbedTLS
// is pure software and behaves identically on every host, which also lets the
// CI runners exercise the RSA paths for real.

#include "core/crypto.h"

#include <mbedtls/build_info.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/asn1.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/bignum.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>

extern "C" {
#include "argon2.h"
}

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace secretkeeper::crypto {
namespace {

// Upper bound for the DER encodings we hold in memory: a PKCS#8 PrivateKeyInfo
// for RSA-2048 is roughly 1.2 KB. 4 KiB is a safety ceiling, not an estimate.
inline constexpr std::size_t kRsaDerMaxBytes = 4096;

[[noreturn]] void fail(const char* what) {
  throw std::runtime_error(std::string(what) + " failed");
}

// mbedTLS reports failure as a NEGATIVE high-bit error code and never as a
// positive value, so `rc < 0` is the correct success test everywhere below.
void check(int rc, const char* what) {
  if (rc < 0) fail(what);
}

// A single process-wide DRBG. Seeding per call would be both slow and less safe:
// the whole point of a DRBG is to stretch one entropy gathering into many bytes.
class GlobalDrbg {
 public:
  static GlobalDrbg& instance() {
    static GlobalDrbg drbg;
    return drbg;
  }

  void random(std::span<std::uint8_t> out) {
    if (out.empty()) return;
    const int rc = mbedtls_ctr_drbg_random(&ctx_, out.data(), out.size());
    if (rc != 0) {
      // A DRBG that has failed is not recoverable; reseeding is the only safe move.
      reseed();
      check(mbedtls_ctr_drbg_random(&ctx_, out.data(), out.size()), "ctr_drbg_random");
    }
  }

  // mbedTLS RSA helpers take the DRBG as an opaque void*; hand them ours.
  mbedtls_ctr_drbg_context* drbg() { return &ctx_; }

 private:
  GlobalDrbg() {
    mbedtls_entropy_init(&entropy_);
    mbedtls_ctr_drbg_init(&ctx_);
    reseed();
  }

  void reseed() {
    // The NIST personalisation string is a fixed, non-secret label; it is not
    // meant to add entropy, only to domain-separate this DRBG from any other in
    // the process.
    static const char kPersonalisation[] = "SecretKeeper DRBG v1";
    check(mbedtls_ctr_drbg_seed(&ctx_, mbedtls_entropy_func, &entropy_,
                                reinterpret_cast<const std::uint8_t*>(kPersonalisation),
                                sizeof(kPersonalisation) - 1),
          "ctr_drbg_seed");
  }

  mbedtls_entropy_context entropy_{};
  mbedtls_ctr_drbg_context ctx_{};

};


// Some mbedTLS entry points return the produced LENGTH on success (e.g.
// mbedtls_pk_write_pubkey_der) instead of 0. Calling check() on those would
// reject every success, so they get their own helper.
int check_len(int rc, const char* what) {
  if (rc < 0) fail(what);
  return rc;
}

// mbedTLS 3.6 keeps the bare PKCS#1 RSAPublicKey codec (mbedtls_rsa_parse_pubkey
// / mbedtls_rsa_write_pubkey) in rsa_internal.h, which is not part of the public
// API. docs/03-data pins the stored public key to exactly that encoding, so the
// two small helpers below do the ASN.1 by hand using only public entry points
// (asn1parse / asn1write / rsa_import / rsa_export). Keeping this local is
// deliberate: it avoids compiling a private header into the product.

// Serialise N and E as  PKCS#1  RSAPublicKey ::= SEQUENCE { n INTEGER, e INTEGER }
// mbedTLS's asn1write helpers fill the buffer BACKWARDS from p, exactly like the
// rest of the library, so the caller must read the result from (end - len).
int write_pkcs1_pubkey(const mbedtls_rsa_context* rsa, std::uint8_t* buf, std::size_t size) {
  mbedtls_mpi n;
  mbedtls_mpi e;
  mbedtls_mpi_init(&n);
  mbedtls_mpi_init(&e);
  int rc = mbedtls_rsa_export(rsa, &n, nullptr, nullptr, nullptr, &e);

  std::size_t total = 0;
  if (rc == 0) {
    // mbedTLS fills the buffer BACKWARDS from p. mbedtls_asn1_write_mpi() already
    // emits the complete INTEGER (tag + length + sign padding + value), so it must
    // not be wrapped again -- doing so produced a 2082-bit "modulus" that silently
    // decrypted to garbage.
    // Order matters: the buffer is filled backwards, so the LAST item written is
    // the FIRST byte of the encoding. Emit E before N so the result reads
    // SEQUENCE { n, e } as DER requires.
    unsigned char* p = buf + size;
    int e_tlv = mbedtls_asn1_write_mpi(&p, buf, &e);
    int n_tlv = 0;
    if (e_tlv < 0) {
      rc = e_tlv;
    } else {
      n_tlv = mbedtls_asn1_write_mpi(&p, buf, &n);
      if (n_tlv < 0) rc = n_tlv;
    }

    if (rc == 0) {
      const std::size_t body = static_cast<std::size_t>(n_tlv) + static_cast<std::size_t>(e_tlv);
      const int seq_hdr = mbedtls_asn1_write_len(&p, buf, body);
      const int seq_tag = (seq_hdr < 0)
                              ? 0
                              : mbedtls_asn1_write_tag(
                                    &p, buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
      if (seq_hdr < 0) rc = seq_hdr;
      else if (seq_tag != 1) rc = MBEDTLS_ERR_ASN1_BUF_TOO_SMALL;
      else total = body + static_cast<std::size_t>(seq_hdr) + 1;
    }

    if (rc == 0 && p != buf) {
      // The encoding begins at p, not at buf.
      std::memmove(buf, p, total);
    }
  }

  mbedtls_mpi_free(&n);
  mbedtls_mpi_free(&e);
  if (rc != 0) return rc;
  return static_cast<int>(total);
}

// Parse  PKCS#1  RSAPublicKey ::= SEQUENCE { n INTEGER, e INTEGER }  into rsa.
int parse_pkcs1_pubkey(mbedtls_rsa_context* rsa, const std::uint8_t* buf,
                       std::size_t size) {
  unsigned char* p = const_cast<unsigned char*>(buf);
  unsigned char* const end = p + size;
  std::size_t len = 0;

  int rc = mbedtls_asn1_get_tag(&p, end, &len,
                                MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
  if (rc != 0) return rc;
  if (end != p + len) return MBEDTLS_ERR_RSA_BAD_INPUT_DATA;

  rc = mbedtls_asn1_get_tag(&p, end, &len, MBEDTLS_ASN1_INTEGER);
  if (rc != 0) return rc;
  rc = mbedtls_rsa_import_raw(rsa, p, len, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);
  if (rc != 0) return MBEDTLS_ERR_RSA_BAD_INPUT_DATA;
  p += len;

  rc = mbedtls_asn1_get_tag(&p, end, &len, MBEDTLS_ASN1_INTEGER);
  if (rc != 0) return rc;
  rc = mbedtls_rsa_import_raw(rsa, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, p, len);
  if (rc != 0) return MBEDTLS_ERR_RSA_BAD_INPUT_DATA;
  return mbedtls_rsa_check_pubkey(rsa);
}

}  // namespace

const char* to_string(CryptoError e) {
  switch (e) {
    case CryptoError::kOk: return "ok";
    case CryptoError::kAuthFailed: return "auth_failed";
    case CryptoError::kInvalidInput: return "invalid_input";
    case CryptoError::kUnsupportedAlgorithm: return "unsupported_algorithm";
    case CryptoError::kInternal: return "internal";
  }
  return "unknown";
}

void SecureBytes::clear() noexcept {
  if (!data_.empty()) {
    // volatile write through a barrier: the compiler must not elide the wipe.
    volatile std::uint8_t* p = data_.data();
    for (std::size_t i = 0; i < data_.size(); ++i) p[i] = 0;
  }
  data_.clear();
  data_.shrink_to_fit();
}

bool has_asymmetric_support() {
  // mbedTLS is pure software: RSA is always available. Kept in the interface so
  // the test suite can assert the skip path is never taken.
  return true;
}

void random_bytes(std::span<std::uint8_t> out) {
  GlobalDrbg::instance().random(out);
}

Id generate_id() {
  Id id{};
  GlobalDrbg::instance().random(std::span<std::uint8_t>(id));
  return id;
}

Kek derive_kek(std::span<const std::uint8_t> password, const Salt& salt) {
  // The vendored Argon2 uses positional arguments rather than the argon2_ctx
  // struct, matching the reference implementation's simple API.
  std::uint8_t out[kKekLength];
  const int rc = argon2id_hash_raw(kKdfIterations, kKdfMemoryKiB, kKdfParallelism,
                                   password.data(), password.size(), salt.data(), salt.size(),
                                   out, kKekLength);
  if (rc != ARGON2_OK) throw std::runtime_error("argon2id_hash_raw failed: " + std::to_string(rc));
  Kek kek{};
  std::memcpy(kek.data(), out, kKekLength);
  // Wipe the staging copy immediately; Kek itself is wiped by its owner.
  volatile std::uint8_t* p = out;
  for (std::size_t i = 0; i < kKekLength; ++i) p[i] = 0;
  return kek;
}

void aes_gcm_encrypt(const std::uint8_t* key, std::size_t key_len,
                     const Nonce& nonce,
                     std::span<const std::uint8_t> aad,
                     std::span<const std::uint8_t> plaintext,
                     std::uint8_t* cipher_out, std::uint8_t* tag_out) {
  if (key == nullptr || key_len != kKekLength) throw std::runtime_error("aes_gcm_encrypt: bad key");
  if (!plaintext.empty() && cipher_out == nullptr) throw std::runtime_error("aes_gcm_encrypt: null out");
  if (tag_out == nullptr) throw std::runtime_error("aes_gcm_encrypt: null tag");

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  // crypt_and_tag only drives starts/update_ad/update/finish -- it never sets the
  // key. The 4th setkey argument is the key length in BITS, not bytes.
  check(mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key,
                           static_cast<unsigned int>(key_len * 8)),
        "gcm_setkey(encrypt)");
  // mbedtls returns MBEDTLS_ERR_GCM_AUTH_FAILED (-0x0012) on tag mismatch; that is
  // an authentication failure, not a corruption we should paper over.
  const int rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, plaintext.size(),
                                           nonce.data(), nonce.size(), aad.data(), aad.size(),
                                           plaintext.data(), cipher_out, kTagLength, tag_out);
  mbedtls_gcm_free(&ctx);
  if (rc != 0) fail("aes_gcm_encrypt");
}

CryptoError aes_gcm_decrypt(const std::uint8_t* key, std::size_t key_len,
                            const Nonce& nonce,
                            std::span<const std::uint8_t> aad,
                            std::span<const std::uint8_t> cipher,
                            const Tag& tag,
                            std::uint8_t* plaintext_out) {
  if (key == nullptr || key_len != kKekLength) return CryptoError::kInvalidInput;

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key,
                         static_cast<unsigned int>(key_len * 8)) != 0) {
    mbedtls_gcm_free(&ctx);
    return CryptoError::kInvalidInput;
  }
  const int rc = mbedtls_gcm_auth_decrypt(&ctx, cipher.size(), nonce.data(), nonce.size(),
                                          aad.data(), aad.size(), tag.data(), kTagLength,
                                          cipher.data(), plaintext_out);
  mbedtls_gcm_free(&ctx);
  if (rc == MBEDTLS_ERR_GCM_BAD_INPUT) return CryptoError::kInvalidInput;
  if (rc != 0) return CryptoError::kAuthFailed;
  return CryptoError::kOk;
}

RsaKeyPair generate_rsa2048() {
  mbedtls_rsa_context rsa;
  mbedtls_rsa_init(&rsa);

  const int rc = mbedtls_rsa_gen_key(&rsa, mbedtls_ctr_drbg_random, GlobalDrbg::instance().drbg(),
                                     static_cast<unsigned int>(kRsaModulusBytes * 8), 65537);
  if (rc != 0) {
    mbedtls_rsa_free(&rsa);
    fail("rsa_gen_key");
  }

  // rsa_gen_key leaves hash_id at MBEDTLS_MD_NONE, and the OAEP helpers read the
  // digest straight out of that field. Trusting the default here yields a key that
  // cannot wrap anything, so pin SHA-256 explicitly: the format spec mandates
  // RSA-OAEP-SHA256 and nothing else.
  mbedtls_rsa_set_padding(&rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);

  // 3.6 removed mbedtls_rsa_info(); mbedtls_pk_info_from_type() replaces it. And
  // mbedtls_pk_setup(ctx, info) ALLOCATES its own rsa context rather than adopting
  // ours -- pk_ctx expands to private_pk_ctx and is not writable from outside.
  // So: let pk_setup allocate, then fill the context it handed us via
  // mbedtls_pk_rsa(). One pk context suffices: it owns a copy of the key material,
  // and the public-only view is produced by write_pubkey_der().
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  RsaKeyPair pair;

  try {
    const mbedtls_pk_info_t* pkinfo = mbedtls_pk_info_from_type(MBEDTLS_PK_RSA);
    check(mbedtls_pk_setup(&pk, pkinfo), "pk_setup");

    mbedtls_rsa_context* dst = mbedtls_pk_rsa(pk);
    if (dst == nullptr) fail("pk_rsa returned null");
    check(mbedtls_rsa_copy(dst, &rsa), "rsa_copy");

    // PKCS#1 RSAPublicKey and PKCS#8 PrivateKeyInfo, both DER, matching
    // docs/03-data. Note that mbedTLS's pk_write_* entry points return the LENGTH
    // written rather than 0 (hence check_len) and fill the buffer BACKWARDS from
    // buf+size, so the encoding begins at (buf + size - len). Buffer sizes here
    // are safety ceilings, not estimates.
    std::array<std::uint8_t, kRsaDerMaxBytes> pub_buf{};
    const int pub_len = check_len(write_pkcs1_pubkey(&rsa, pub_buf.data(), pub_buf.size()),
                                  "write_pkcs1_pubkey");
    pair.public_der = SecureBytes(pub_buf.data(), static_cast<std::size_t>(pub_len));

    std::array<std::uint8_t, kRsaDerMaxBytes> priv_buf{};
    const int priv_len =
        check_len(mbedtls_pk_write_key_der(&pk, priv_buf.data(), priv_buf.size()),
                  "write_key_der");
    pair.private_der = SecureBytes(priv_buf.data() + (priv_buf.size() - static_cast<std::size_t>(priv_len)),
                                   static_cast<std::size_t>(priv_len));
  } catch (...) {
    mbedtls_pk_free(&pk);
    mbedtls_rsa_free(&rsa);
    throw;
  }

  mbedtls_pk_free(&pk);
  mbedtls_rsa_free(&rsa);
  return pair;
}

std::optional<std::array<std::uint8_t, kRsaModulusBytes>> rsa_oaep_encrypt(
    std::span<const std::uint8_t> public_der, std::span<const std::uint8_t> data_key) {
  if (public_der.empty() || data_key.size() != kDataKeyLength) return std::nullopt;

  // docs/03-data stores the public key as a bare PKCS#1 RSAPublicKey. Accept a
  // SubjectPublicKeyInfo too, since that is the other encoding mbedTLS (and the
  // other platforms) may reasonably emit.
  mbedtls_rsa_context rsa;
  mbedtls_rsa_init(&rsa);
  if (parse_pkcs1_pubkey(&rsa, public_der.data(), public_der.size()) != 0) {
    mbedtls_rsa_free(&rsa);
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    if (mbedtls_pk_parse_public_key(&pk, public_der.data(), public_der.size()) != 0 ||
        mbedtls_pk_get_type(&pk) != MBEDTLS_PK_RSA) {
      mbedtls_pk_free(&pk);
      return std::nullopt;
    }
    check(mbedtls_rsa_copy(&rsa, mbedtls_pk_rsa(pk)), "rsa_copy");
    mbedtls_pk_free(&pk);
  }

  std::array<std::uint8_t, kRsaModulusBytes> out{};
  // Empty label == standard OAEP, matching label: null in test-vectors/rsa-oaep.json.
  // The digest comes from ctx->hash_id, which a bare PKCS#1 parse leaves unset, so
  // pin SHA-256 explicitly instead of trusting whatever the DER carried.
  mbedtls_rsa_set_padding(&rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
  const int rc = mbedtls_rsa_rsaes_oaep_encrypt(&rsa, mbedtls_ctr_drbg_random,
                                                GlobalDrbg::instance().drbg(), nullptr, 0,
                                                data_key.size(), data_key.data(), out.data());
  mbedtls_rsa_free(&rsa);
  if (rc != 0) return std::nullopt;
  return out;
}

std::optional<DataKey> rsa_oaep_decrypt(std::span<const std::uint8_t> private_der,
                                        std::span<const std::uint8_t> wrapped) {
  if (private_der.empty() || wrapped.size() != kRsaModulusBytes) return std::nullopt;

  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  if (mbedtls_pk_parse_key(&pk, private_der.data(), private_der.size(), nullptr, 0,
                          mbedtls_ctr_drbg_random, GlobalDrbg::instance().drbg()) != 0) {
    mbedtls_pk_free(&pk);
    return std::nullopt;
  }

  DataKey out{};
  std::size_t out_len = out.size();
  if (mbedtls_pk_get_type(&pk) != MBEDTLS_PK_RSA) {
    mbedtls_pk_free(&pk);
    return std::nullopt;
  }
  mbedtls_rsa_set_padding(mbedtls_pk_rsa(pk), MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
  const int rc = mbedtls_rsa_rsaes_oaep_decrypt(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random,
                                                GlobalDrbg::instance().drbg(), nullptr, 0,
                                                &out_len, wrapped.data(), out.data(),
                                                out.size());
  mbedtls_pk_free(&pk);
  // OAEP unpadding failures and authentication failures are indistinguishable by
  // design; both mean "wrong key or tampered ciphertext".
  if (rc != 0 || out_len != out.size()) return std::nullopt;
  return out;
}

}  // namespace secretkeeper::crypto
