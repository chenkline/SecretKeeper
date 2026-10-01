#pragma once

// 机密心 - 密码学层公共接口
//
// 各平台实现保持同名同语义，便于对照实现与排查。
// 本文件只声明原语，不含业务语义；所有密钥材料均要求调用方传入可清零缓冲区。

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace secretkeeper::crypto {

// ---- 固定参数（与 docs/02-crypto 一致，任何变更须重生成 test-vectors）----
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

// 可清零的字节缓冲，用于承载密钥材料；析构时自动抹除。
class SecureBytes {
 public:
  SecureBytes() = default;
  explicit SecureBytes(std::size_t n) : data_(n, 0) {}
  SecureBytes(const std::uint8_t* p, std::size_t n) : data_(p, p + n) {}
  // 密钥材料禁止拷贝：拷贝会产生无法追踪的副本，违背清零约定。
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

// ---- 错误类型：区分认证失败与其它失败，对外统一映射为密码错误 ----
enum class CryptoError {
  kOk = 0,
  kAuthFailed,
  kInvalidInput,
  kUnsupportedAlgorithm,
  kInternal,
};

const char* to_string(CryptoError e);

// ---- 环境能力探测 ----
// 精简版 / 容器化 Windows 镜像可能只提供 CNG 对称算法（AES、SHA、ChaCha20），
// 而不提供 RSA / ECC / DH。此时 RSA 相关调用会返回 STATUS_INVALID_HANDLE 或
// STATUS_NOT_SUPPORTED。启动时探测一次，避免把环境缺陷误报为实现缺陷。
bool cng_has_asymmetric_support();

// ---- 随机数（CNG BCryptGenRandom，不得使用非密码学随机源）----
void random_bytes(std::span<std::uint8_t> out);
Id generate_id();

// ---- 密钥派生 ----
// KEK = Argon2id(password, salt, m, t, p, outlen=32)
// salt 须由调用方生成并随记录保存，这是跨端一致的前提。
Kek derive_kek(std::span<const std::uint8_t> password, const Salt& salt);

// ---- AES-256-GCM ----
// 公钥与私钥加密必须传入互不相同的 nonce；复用会导致密钥泄露。
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

// 公钥封装数据密钥：RSA-OAEP，MGF1 与 OAEP 哈希均为 SHA-256（显式指定，
// 不可依赖各库默认值）。失败返回 nullopt。
std::optional<std::array<std::uint8_t, kRsaModulusBytes>> rsa_oaep_encrypt(
    std::span<const std::uint8_t> public_der, std::span<const std::uint8_t> data_key);

std::optional<DataKey> rsa_oaep_decrypt(
    std::span<const std::uint8_t> private_der,
    std::span<const std::uint8_t> wrapped);

}  // namespace secretkeeper::crypto
