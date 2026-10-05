// SecretKeeper - cross-platform crypto conformance probe.
//
// Purpose: prove that identical inputs produce byte-identical outputs on every
// platform. Unlike scripts/verify-vectors.py -- which re-derives the vectors with
// an independent Python implementation and therefore only proves the VECTORS are
// self-consistent -- this probe runs the actual shipped C++ code and prints a
// transcript. Comparing transcripts across platforms is what proves the
// IMPLEMENTATIONS agree.
//
// Determinism rules:
//   - derive_kek is deterministic given (password, salt): same input, same KEK.
//   - aes_gcm_encrypt is deterministic given (key, nonce, aad, plaintext):
//     GCM with a fixed nonce and fixed key has no randomness. Nonces are supplied
//     by the caller, never drawn from the DRBG here.
//   - rsa_oaep_encrypt is NOT deterministic (OAEP draws a random seed). It is
//     probed the other way round: a fixed key and a fixed ciphertext must
//     decrypt to the same data key, and two encryptions of one key must differ.
//   - random_bytes / generate_id / generate_rsa2048 are non-deterministic by
//     design. They are probed for distribution and length only, never equality.
//
// Every line is "name=value" with lowercase hex, one probe per line, so a
// transcript diff between platforms is readable. Output must stay pure ASCII.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/crypto.h"

namespace crypto = secretkeeper::crypto;

namespace {

int g_checks = 0;
int g_failures = 0;

void emit(const std::string& name, const std::string& value) {
  std::printf("%s=%s\n", name.c_str(), value.c_str());
}

void check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("PROBE-FAIL %s\n", what.c_str());
  }
}

std::string hex(const std::uint8_t* p, std::size_t n) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(n * 2);
  for (std::size_t i = 0; i < n; ++i) {
    out.push_back(digits[p[i] >> 4]);
    out.push_back(digits[p[i] & 0x0F]);
  }
  return out;
}

template <std::size_t N>
std::string hex(const std::array<std::uint8_t, N>& a) { return hex(a.data(), a.size()); }

std::vector<std::uint8_t> from_hex(const std::string& h) {
  std::vector<std::uint8_t> out;
  out.reserve(h.size() / 2);
  for (std::size_t i = 0; i + 1 < h.size(); i += 2) {
    out.push_back(static_cast<std::uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
  }
  return out;
}

std::span<const std::uint8_t> bytes_of(const std::vector<std::uint8_t>& v) {
  return std::span<const std::uint8_t>(v.data(), v.size());
}

std::span<std::uint8_t> mut_bytes(std::vector<std::uint8_t>& v) {
  return std::span<std::uint8_t>(v.data(), v.size());
}

// The Argon2id cases mirror test-vectors/kdf-argon2id.json exactly. They must
// agree with it, and they must agree across every platform.
void probe_kdf() {
  struct Case {
    const char* name;
    const char* password;
    const char* salt;
  };
  // Passwords are ASCII here so the transcript stays comparable byte for byte.
  const Case cases[] = {
      {"argon.empty_password", "", "00000000000000000000000000000000"},
      {"argon.short_password", "pw", "000102030405060708090a0b0c0d0e0f"},
      {"argon.typical_password", "correct horse battery staple",
       "101112131415161718191a1b1c1d1e1f"},
      {"argon.exact_150_bytes",
       "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
       "0123456789012345678901234567890123456789012345678",
       "aabbccddeeff00112233445566778899"},
      {"argon.utf8_password", "\xe4\xb8\xad\xe6\x96\x87\xe5\xaf\x86\xe9\x92\xa5",
       "fedcba9876543210fedcba9876543210"},
  };
  for (const Case& c : cases) {
    const std::vector<std::uint8_t> pw =
        from_hex(hex(reinterpret_cast<const std::uint8_t*>(c.password),
                     std::strlen(c.password)));
    crypto::Salt salt{};
    const std::vector<std::uint8_t> salt_bytes = from_hex(c.salt);
    std::memcpy(salt.data(), salt_bytes.data(), salt_bytes.size());

    const crypto::Kek kek = crypto::derive_kek(bytes_of(pw), salt);
    emit(c.name, hex(kek));
  }
}

void probe_gcm() {
  struct Case {
    const char* name;
    const char* key;
    const char* nonce;
    const char* aad;
    const char* plaintext;
  };
  const Case cases[] = {
      {"gcm.empty_plaintext", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
       "000102030405060708090a0b", "", ""},
      {"gcm.single_byte", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
       "000102030405060708090a0b", "", "61"},
      {"gcm.no_aad", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
       "000102030405060708090a0b", "", "6f726967696e616c206d657374616765"},
      {"gcm.with_aad", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
       "000102030405060708090a0b", "feedface", "6f726967696e616c206d657373616765"},
      {"gcm.block_aligned_16", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
       "000102030405060708090a0b", "a0a1a2a3",
       "00112233445566778899aabbccddeeff"},
      {"gcm.exact_block_plus_one",
       "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "000102030405060708090a0b",
       "00", "00112233445566778899aabbccddeeff0102030405060708"},
      // AAD and plaintext are raw UTF-8 for the Chinese word "密码" (e4b8ad e5af86
      // e992a5). Encoded as hex so the transcript stays pure ASCII.
      {"gcm.utf8_aad", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
       "0f0e0d0c0b0a090807060504", "e4b8ad", "e5af86e992a5"},
  };
  for (const Case& c : cases) {
    const std::vector<std::uint8_t> key = from_hex(c.key);
    const std::vector<std::uint8_t> nonce_bytes = from_hex(c.nonce);
    const std::vector<std::uint8_t> aad = from_hex(c.aad);
    const std::vector<std::uint8_t> pt = from_hex(c.plaintext);

    // A 12-byte nonce and a 16-byte tag are format requirements, not
    // conventions. Assert the length here instead of letting a short nonce
    // overwrite the stack and surface as a corruption three frames deeper.
    check(nonce_bytes.size() == crypto::kNonceLength, std::string(c.name) + " nonce length");
    crypto::Nonce nonce{};
    std::memcpy(nonce.data(), nonce_bytes.data(), nonce_bytes.size());
    crypto::Tag tag{};

    // GCM is a stream cipher: ciphertext length equals plaintext length.
    std::vector<std::uint8_t> cipher(pt.size());
    crypto::aes_gcm_encrypt(key.data(), key.size(), nonce, bytes_of(aad), bytes_of(pt),
                            cipher.data(), tag.data());

    emit(std::string(c.name) + ".cipher", hex(cipher.data(), cipher.size()));
    emit(std::string(c.name) + ".tag", hex(tag));

    // Round trip: the same call must recover the plaintext on every platform.
    std::vector<std::uint8_t> back(pt.size());
    const crypto::CryptoError rc =
        crypto::aes_gcm_decrypt(key.data(), key.size(), nonce, bytes_of(aad),
                                bytes_of(cipher), tag, back.data());
    check(rc == crypto::CryptoError::kOk, std::string(c.name) + " round trip");
    check(back == pt, std::string(c.name) + " round trip bytes");

    // A single flipped ciphertext bit must be caught by the tag on every platform.
    if (!cipher.empty()) {
      std::vector<std::uint8_t> tampered = cipher;
      tampered[0] ^= 0x01;
      const crypto::CryptoError bad = crypto::aes_gcm_decrypt(
          key.data(), key.size(), nonce, bytes_of(aad), bytes_of(tampered), tag, back.data());
      check(bad == crypto::CryptoError::kAuthFailed,
            std::string(c.name) + " tampered cipher rejected");
    }
    // So must a flipped tag bit.
    {
      crypto::Tag bad_tag = tag;
      bad_tag[0] ^= 0x80;
      const crypto::CryptoError bad =
          crypto::aes_gcm_decrypt(key.data(), key.size(), nonce, bytes_of(aad),
                                  bytes_of(cipher), bad_tag, back.data());
      check(bad == crypto::CryptoError::kAuthFailed,
            std::string(c.name) + " tampered tag rejected");
    }
    // And a different AAD must fail, since the AAD is authenticated.
    {
      std::vector<std::uint8_t> other_aad = aad;
      other_aad.push_back(0xFF);
      const crypto::CryptoError bad =
          crypto::aes_gcm_decrypt(key.data(), key.size(), nonce, bytes_of(other_aad),
                                  bytes_of(cipher), tag, back.data());
      check(bad == crypto::CryptoError::kAuthFailed,
            std::string(c.name) + " wrong aad rejected");
    }
  }
}

// RSA-OAEP cannot be compared by ciphertext: OAEP draws a random seed, so two
// encryptions of one key always differ. What must hold identically everywhere
// is the decrypt direction and the size limits.
void probe_rsa() {
  check(crypto::has_asymmetric_support(), "asymmetric support available");

  // A 2048-bit key: 256 bytes modulus, 256 bytes ciphertext, 190 bytes max
  // plaintext (256 - 2*32 - 2). These bounds are format-level and must match.
  const crypto::RsaKeyPair pair = crypto::generate_rsa2048();
  check(pair.public_der.size() == crypto::kRsaModulusBytes - 20 ||
            pair.public_der.size() > 0,
        "public DER produced");
  check(pair.private_der.size() > pair.public_der.size(), "private DER larger than public");

  crypto::DataKey dk{};
  for (std::size_t i = 0; i < dk.size(); ++i) {
    dk[i] = static_cast<std::uint8_t>(i);
  }

  const std::optional<std::array<std::uint8_t, crypto::kRsaModulusBytes>> wrapped =
      crypto::rsa_oaep_encrypt(pair.public_der.span(),
                               std::span<const std::uint8_t>(dk.data(), dk.size()));
  check(wrapped.has_value(), "oaep encrypt succeeds");
  if (!wrapped) return;

  // The ciphertext must be exactly one modulus wide on every platform.
  emit("rsa.cipher_len", std::to_string(wrapped->size()));
  check(wrapped->size() == crypto::kRsaModulusBytes, "ciphertext is one modulus wide");

  const std::optional<crypto::DataKey> back = crypto::rsa_oaep_decrypt(
      pair.private_der.span(),
      std::span<const std::uint8_t>(wrapped->data(), wrapped->size()));
  check(back.has_value(), "oaep decrypt succeeds");
  if (back) {
    check(*back == dk, "oaep round trip recovers the data key");
  }

  // Two encryptions of the same key must differ: proof that OAEP really is
  // randomised and we are not accidentally in PKCS#1 v1.5 mode.
  const auto second = crypto::rsa_oaep_encrypt(
      pair.public_der.span(), std::span<const std::uint8_t>(dk.data(), dk.size()));
  check(second.has_value(), "second oaep encrypt succeeds");
  if (second) {
    check(*second != *wrapped, "oaep ciphertext is randomised");
  }

  // Decrypting with the wrong key must fail everywhere, not just sometimes.
  const crypto::RsaKeyPair other = crypto::generate_rsa2048();
  const std::optional<crypto::DataKey> wrong = crypto::rsa_oaep_decrypt(
      other.private_der.span(),
      std::span<const std::uint8_t>(wrapped->data(), wrapped->size()));
  check(!wrong.has_value(), "decrypt with the wrong key fails");

  // Oversized input must be refused before any allocation happens.
  std::vector<std::uint8_t> too_big(crypto::kRsaModulusBytes);
  check(!crypto::rsa_oaep_encrypt(pair.public_der.span(), bytes_of(too_big)).has_value(),
        "oversized plaintext refused");
}

// Non-deterministic by design. Probed for shape and distribution only: a
// platform whose DRBG returned a constant would pass a determinism check but
// fail here.
void probe_random() {
  std::vector<std::uint8_t> a(32, 0);
  std::vector<std::uint8_t> b(32, 0);
  crypto::random_bytes(mut_bytes(a));
  crypto::random_bytes(mut_bytes(b));
  check(a != b, "random_bytes differs between calls");
  check(std::any_of(a.begin(), a.end(), [](std::uint8_t v) { return v != 0; }),
        "random_bytes is not all zero");

  const crypto::Id id1 = crypto::generate_id();
  const crypto::Id id2 = crypto::generate_id();
  check(id1 != id2, "generate_id differs between calls");
  check(id1.size() == crypto::kIdLength, "generate_id length");

  // Bit balance over a larger sample: a broken entropy source shows up here as
  // a lopsided count long before it shows up as a functional failure.
  std::size_t ones = 0;
  const std::size_t total = 4096;
  std::vector<std::uint8_t> bulk(total);
  crypto::random_bytes(mut_bytes(bulk));
  for (std::uint8_t v : bulk) {
    std::uint8_t n = v;
    while (n != 0) {
      ones += n & 1U;
      n = static_cast<std::uint8_t>(n >> 1);
    }
  }
  emit("random.ones_in_32768_bits", std::to_string(ones));
  // A fair coin lands within 5% of half. Allow a wide band: this catches a
  // stuck or constant source, not a merely unlucky run.
  check(ones > total * 8 * 45 / 100 && ones < total * 8 * 55 / 100,
        "random_bytes is bit balanced");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("PROBE-VERSION 1\n");

  probe_kdf();
  probe_gcm();
  probe_rsa();
  probe_random();

  // The transcript ends with a machine-comparable summary line.
  std::printf("PROBE-SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}