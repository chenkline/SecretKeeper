// 机密心 - Windows 密码学层实现
//
// CNG（bcrypt.dll）负责 AES-256-GCM、RSA-2048 与系统随机源；
// Argon2id 使用内置的 Argon2 参考实现（vendor/argon2，CC0/Apache-2.0）。
//
// 重要背景：Windows 11 的 CNG **不提供 Argon2id**
// （BCryptOpenAlgorithmProvider("ARGON2ID") 返回 STATUS_NOT_FOUND），
// 因此不能依赖系统 API，必须自带实现。这也是五端共用同一份 Argon2 源码、
// 再各自用黄金向量校验的原因。

#include "crypto.h"

#include "der.h"

#include <windows.h>
#include <bcrypt.h>

extern "C" {
#include "argon2.h"
}

#include <cstring>
#include <stdexcept>
#include <string>

#pragma comment(lib, "bcrypt.lib")

namespace secretkeeper::crypto {
namespace {

void secure_zero(void* p, std::size_t n) {
  if (p && n) ::SecureZeroMemory(p, n);
}

class BcryptKey {
 public:
  BcryptKey() = default;
  // 接管已有句柄，所有权转移给本对象，析构时统一释放。
  explicit BcryptKey(BCRYPT_KEY_HANDLE handle) : handle_(handle) {}
  ~BcryptKey() {
    if (handle_) BCryptDestroyKey(handle_);
  }
  BcryptKey(const BcryptKey&) = delete;
  BcryptKey& operator=(const BcryptKey&) = delete;
  BCRYPT_KEY_HANDLE get() const { return handle_; }

 private:
  BCRYPT_KEY_HANDLE handle_ = nullptr;
};

class BcryptAlg {
 public:
  BcryptAlg() = default;
  ~BcryptAlg() {
    if (handle_) BCryptCloseAlgorithmProvider(handle_, 0);
  }
  BcryptAlg(const BcryptAlg&) = delete;
  BcryptAlg& operator=(const BcryptAlg&) = delete;
  BCRYPT_ALG_HANDLE get() const { return handle_; }
  BCRYPT_ALG_HANDLE* put() { return &handle_; }

 private:
  BCRYPT_ALG_HANDLE handle_ = nullptr;
};

// Argon2 的 10 MiB 工作内存走 HeapAlloc（含 MEM_RESET_ON_ALLOC），
// 分配失败时 Argon2 会自行返回内存分配错误，无需在此处理。
int argon_alloc(std::uint8_t** memory, std::size_t bytes) {
  *memory = static_cast<std::uint8_t*>(
      ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, bytes));
  if (*memory == nullptr) return ARGON2_MEMORY_ALLOCATION_ERROR;
  return ARGON2_OK;
}

void argon_free(std::uint8_t* memory, std::size_t) {
  if (memory) ::HeapFree(::GetProcessHeap(), 0, memory);
}

[[noreturn]] void throw_nt(const char* what, NTSTATUS st) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%08lX",
                static_cast<unsigned long>(st));
  throw std::runtime_error(std::string(what) + " 失败: " + buf);
}

void check(NTSTATUS st, const char* what) {
  if (st < 0) throw_nt(what, st);
}

// CNG 的 GCM 把认证标签追加在密文尾部，本项目在接口层把两者分开传入。
// 这里记录密文长度以便拆出 tag。
struct GcmSplit {
  std::size_t cipher_len;
  std::uint8_t tag[kTagLength];
};

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
    secure_zero(data_.data(), data_.size());
    data_.clear();
  }
}

bool cng_has_asymmetric_support() {
  // BCryptGetProperty(BCRYPT_OBJECT_LENGTH) 对可用的密钥类算法返回成功；
  // 缺少非对称提供者时返回 STATUS_NOT_SUPPORTED。用它做一次性探测。
  static const bool supported = [] {
    BcryptAlg alg;
    if (BCryptOpenAlgorithmProvider(alg.put(), BCRYPT_RSA_ALGORITHM, nullptr,
                                    0) < 0) {
      return false;
    }
    ULONG object_length = 0;
    ULONG result_size = 0;
    const NTSTATUS st =
        BCryptGetProperty(alg.get(), BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_length),
                          sizeof(object_length), &result_size, 0);
    return st >= 0;
  }();
  return supported;
}

void random_bytes(std::span<std::uint8_t> out) {
  check(BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(out.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG),
        "BCryptGenRandom");
}

Id generate_id() {
  Id id{};
  random_bytes(std::span<std::uint8_t>(id.data(), id.size()));
  return id;
}

// ---------------------------------------------------------------- Argon2id
// KEK = Argon2id(password, salt, m=10240KiB, t=3, p=1, outlen=32, version=19)
// 密码按 UTF-8 字节传入；盐由调用方生成并随记录明文保存（跨端一致的前提）。
// 传空的 secret 与 ad，对应向量中的无附加输入情形。

Kek derive_kek(std::span<const std::uint8_t> password, const Salt& salt) {
  // argon2_context 的密码/盐字段非 const，故复制到可写缓冲。
  std::vector<std::uint8_t> pwd(password.begin(), password.end());
  std::vector<std::uint8_t> salt_buf(salt.begin(), salt.end());
  Kek out{};

  argon2_context ctx{};
  ctx.out = out.data();
  ctx.outlen = static_cast<std::uint32_t>(out.size());
  ctx.pwd = pwd.data();
  ctx.pwdlen = static_cast<std::uint32_t>(pwd.size());
  ctx.salt = salt_buf.data();
  ctx.saltlen = static_cast<std::uint32_t>(salt_buf.size());
  ctx.secret = nullptr;
  ctx.secretlen = 0;
  ctx.ad = nullptr;
  ctx.adlen = 0;
  ctx.t_cost = kKdfIterations;
  ctx.m_cost = kKdfMemoryKiB;
  ctx.lanes = kKdfParallelism;
  ctx.threads = kKdfParallelism;
  ctx.version = kKdfVersion;
  ctx.allocate_cbk = argon_alloc;
  ctx.free_cbk = argon_free;
  // 清空密码副本，但不清 out（那是返回值）。
  ctx.flags = ARGON2_FLAG_CLEAR_PASSWORD;

  const int rc = argon2id_ctx(&ctx);
  secure_zero(pwd.data(), pwd.size());
  secure_zero(salt_buf.data(), salt_buf.size());
  if (rc != ARGON2_OK) {
    secure_zero(out.data(), out.size());
    throw std::runtime_error(std::string("Argon2id 失败: ") +
                             argon2_error_message(rc));
  }
  return out;
}

// -------------------------------------------------------------- AES-256-GCM

void aes_gcm_encrypt(const std::uint8_t* key, std::size_t key_len,
                     const Nonce& nonce,
                     std::span<const std::uint8_t> aad,
                     std::span<const std::uint8_t> plaintext,
                     std::uint8_t* cipher_out, std::uint8_t* tag_out) {
  if (key_len != 32) throw std::runtime_error("AES-256-GCM 密钥长度必须为 32");

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info{};
  info.cbSize = sizeof(info);
  info.dwInfoVersion = BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION;
  info.pbNonce = const_cast<PUCHAR>(nonce.data());
  info.cbNonce = static_cast<ULONG>(nonce.size());
  info.pbAuthData = aad.empty() ? nullptr
                                : const_cast<PUCHAR>(aad.data());
  info.cbAuthData = static_cast<ULONG>(aad.size());
  info.pbTag = tag_out;
  info.cbTag = static_cast<ULONG>(kTagLength);
  info.pbMacContext = nullptr;
  info.cbMacContext = 0;

  BcryptAlg alg;
  check(BCryptOpenAlgorithmProvider(alg.put(), BCRYPT_AES_ALGORITHM, nullptr, 0),
        "BCryptOpenAlgorithmProvider(AES)");

  // CNG 的 GCM 模式通过 CHAINING_MODE 属性开启，默认是 CBC。
  const std::wstring kGcm = L"ChainingModeGCM";
  check(BCryptSetProperty(alg.get(), BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(
                              kGcm.c_str())),
                          static_cast<ULONG>(kGcm.size() * sizeof(wchar_t)), 0),
        "BCryptSetProperty(CHAINING_MODE=GCM)");

  BCRYPT_KEY_HANDLE hkey = nullptr;
  check(BCryptGenerateSymmetricKey(alg.get(), &hkey, nullptr, 0,
                                   const_cast<PUCHAR>(key),
                                   static_cast<ULONG>(key_len), 0),
        "BCryptGenerateSymmetricKey");
  BcryptKey key_handle{hkey};

  // GCM 模式下密文长度恒等于明文长度（无填充），认证标签经 pbTag 单独输出。
  // 不使用「先探测长度再分配」的写法：BCryptEncrypt 的探测调用对 GCM 返回的
  // pcbResult 不可靠，据此分配会得到错误大小的缓冲。
  const ULONG capacity = static_cast<ULONG>(plaintext.size()) + kTagLength;
  std::vector<std::uint8_t> buffer(capacity);
  ULONG written = 0;
  const NTSTATUS st =
      BCryptEncrypt(key_handle.get(), const_cast<PUCHAR>(plaintext.data()),
                    static_cast<ULONG>(plaintext.size()), &info, nullptr, 0,
                    buffer.data(), capacity, &written, 0);
  check(st, "BCryptEncrypt");
  if (written != plaintext.size()) {
    throw std::runtime_error("AES-GCM 密文长度异常");
  }
  if (!plaintext.empty()) {
    std::memcpy(cipher_out, buffer.data(), written);
  }
  secure_zero(buffer.data(), buffer.size());
}

CryptoError aes_gcm_decrypt(const std::uint8_t* key, std::size_t key_len,
                            const Nonce& nonce,
                            std::span<const std::uint8_t> aad,
                            std::span<const std::uint8_t> cipher,
                            const Tag& tag,
                            std::uint8_t* plaintext_out) {
  if (key_len != 32) return CryptoError::kInvalidInput;

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info{};
  info.cbSize = sizeof(info);
  info.dwInfoVersion = BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION;
  info.pbNonce = const_cast<PUCHAR>(nonce.data());
  info.cbNonce = static_cast<ULONG>(nonce.size());
  info.pbAuthData = aad.empty() ? nullptr : const_cast<PUCHAR>(aad.data());
  info.cbAuthData = static_cast<ULONG>(aad.size());
  info.pbTag = const_cast<PUCHAR>(tag.data());
  info.cbTag = static_cast<ULONG>(kTagLength);
  info.pbMacContext = nullptr;
  info.cbMacContext = 0;

  BcryptAlg alg;
  if (BCryptOpenAlgorithmProvider(alg.put(), BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) {
    return CryptoError::kInternal;
  }
  const std::wstring kGcm = L"ChainingModeGCM";
  if (BCryptSetProperty(alg.get(), BCRYPT_CHAINING_MODE,
                        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(
                            kGcm.c_str())),
                        static_cast<ULONG>(kGcm.size() * sizeof(wchar_t)),
                        0) < 0) {
    return CryptoError::kInternal;
  }

  BCRYPT_KEY_HANDLE hkey = nullptr;
  if (BCryptGenerateSymmetricKey(alg.get(), &hkey, nullptr, 0,
                                 const_cast<PUCHAR>(key),
                                 static_cast<ULONG>(key_len), 0) < 0) {
    return CryptoError::kInternal;
  }
  BcryptKey key_handle{hkey};

  // 空明文也必须走一次解密以校验认证标签。
  ULONG capacity = static_cast<ULONG>(cipher.size() + kTagLength + 16);
  std::vector<std::uint8_t> plain(capacity);
  ULONG written = 0;
  NTSTATUS st = BCryptDecrypt(key_handle.get(),
                              const_cast<PUCHAR>(cipher.data()),
                              static_cast<ULONG>(cipher.size()),
                              &info, nullptr, 0, plain.data(), capacity,
                              &written, 0);
  if (st < 0) {
    secure_zero(plain.data(), plain.size());
    // 认证标签不匹配与密文篡改都落到这里，对外统一视为密码错误。
    return CryptoError::kAuthFailed;
  }
  if (!cipher.empty()) {
    std::memcpy(plaintext_out, plain.data(), written);
  }
  secure_zero(plain.data(), plain.size());
  return CryptoError::kOk;
}

// ------------------------------------------------------------------ RSA-2048

RsaKeyPair generate_rsa2048() {
  BcryptAlg alg;
  check(BCryptOpenAlgorithmProvider(alg.put(), BCRYPT_RSA_ALGORITHM, nullptr, 0),
        "BCryptOpenAlgorithmProvider(RSA)");

  BCRYPT_KEY_HANDLE hkey = nullptr;
  check(BCryptGenerateKeyPair(alg.get(), &hkey, 2048, 0),
        "BCryptGenerateKeyPair");
  BcryptKey key{hkey};

  // CNG 的 RSAFULLPRIVATEBLOB 内含 p/q/dp/dq/qInv，可据此还原私钥指数 d。
  ULONG pub_len = 0;
  check(BCryptExportKey(key.get(), nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0,
                        &pub_len, 0),
        "BCryptExportKey(public) 长度探测");
  std::vector<std::uint8_t> pub(pub_len);
  check(BCryptExportKey(key.get(), nullptr, BCRYPT_RSAPUBLIC_BLOB, pub.data(),
                        pub_len, &pub_len, 0),
        "BCryptExportKey(public)");

  ULONG priv_len = 0;
  check(BCryptExportKey(key.get(), nullptr, BCRYPT_RSAFULLPRIVATE_BLOB, nullptr,
                        0, &priv_len, 0),
        "BCryptExportKey(private) 长度探测");
  std::vector<std::uint8_t> priv(priv_len);
  check(BCryptExportKey(key.get(), nullptr, BCRYPT_RSAFULLPRIVATE_BLOB,
                        priv.data(), priv_len, &priv_len, 0),
        "BCryptExportKey(private)");

  // CNG blob -> 大端整数 -> DER
  auto pub_key = der::from_cng_public_blob(pub.data(), pub.size());
  if (!pub_key) throw std::runtime_error("CNG 公钥 blob 解析失败");

  RsaKeyPair out;
  auto pub_der = der::write_pkcs1_public(*pub_key);
  const auto priv_key = der::from_cng_private_blob(priv.data(), priv.size());
  if (!priv_key) throw std::runtime_error("CNG 私钥 blob 解析或 d 还原失败");
  auto priv_der = der::write_pkcs8_private(*priv_key);
  out.public_der = SecureBytes(pub_der.data(), pub_der.size());
  out.private_der = SecureBytes(priv_der.data(), priv_der.size());
  secure_zero(pub_der.data(), pub_der.size());
  secure_zero(priv_der.data(), priv_der.size());
  secure_zero(pub.data(), pub.size());
  secure_zero(priv.data(), priv.size());
  return out;
}

std::optional<std::array<std::uint8_t, kRsaModulusBytes>> rsa_oaep_encrypt(
    std::span<const std::uint8_t> public_der,
    std::span<const std::uint8_t> data_key) {
  auto key = der::parse_pkcs1_public(public_der.data(), public_der.size());
  if (!key) return std::nullopt;
  auto blob = der::to_cng_public_blob(*key);

  BCRYPT_OAEP_PADDING_INFO oaep{};
  oaep.pszAlgId = BCRYPT_SHA256_ALGORITHM;
  oaep.cbLabel = 0;
  oaep.pbLabel = nullptr;

  BcryptAlg alg;
  if (BCryptOpenAlgorithmProvider(alg.put(), BCRYPT_RSA_ALGORITHM, nullptr, 0) < 0) {
    return std::nullopt;
  }
  BCRYPT_KEY_HANDLE hkey = nullptr;
  if (BCryptImportKeyPair(alg.get(), nullptr,
                          BCRYPT_RSAPUBLIC_BLOB, &hkey,
                          const_cast<PUCHAR>(blob.data()),
                          static_cast<ULONG>(blob.size()), 0) < 0) {
    return std::nullopt;
  }
  BcryptKey kh{hkey};

  ULONG len = 0;
  if (BCryptEncrypt(kh.get(), const_cast<PUCHAR>(data_key.data()),
                    static_cast<ULONG>(data_key.size()), &oaep, nullptr, 0,
                    nullptr, 0, &len, BCRYPT_PAD_OAEP) < 0) {
    return std::nullopt;
  }
  if (len != kRsaModulusBytes) return std::nullopt;
  std::array<std::uint8_t, kRsaModulusBytes> out{};
  ULONG written = 0;
  if (BCryptEncrypt(kh.get(), const_cast<PUCHAR>(data_key.data()),
                    static_cast<ULONG>(data_key.size()), &oaep, nullptr, 0,
                    out.data(), static_cast<ULONG>(out.size()), &written,
                    BCRYPT_PAD_OAEP) < 0) {
    return std::nullopt;
  }
  if (written != out.size()) return std::nullopt;
  return out;
}

std::optional<DataKey> rsa_oaep_decrypt(
    std::span<const std::uint8_t> private_der,
    std::span<const std::uint8_t> wrapped) {
  auto blob = der::pkcs8_to_cng_private_blob(private_der.data(), private_der.size());
  if (!blob) return std::nullopt;

  BCRYPT_OAEP_PADDING_INFO oaep{};
  oaep.pszAlgId = BCRYPT_SHA256_ALGORITHM;
  oaep.cbLabel = 0;
  oaep.pbLabel = nullptr;

  BcryptAlg alg;
  if (BCryptOpenAlgorithmProvider(alg.put(), BCRYPT_RSA_ALGORITHM, nullptr, 0) < 0) {
    return std::nullopt;
  }
  BCRYPT_KEY_HANDLE hkey = nullptr;
  if (BCryptImportKeyPair(alg.get(), nullptr, BCRYPT_RSAFULLPRIVATE_BLOB, &hkey,
                          const_cast<PUCHAR>(blob->data()),
                          static_cast<ULONG>(blob->size()), 0) < 0) {
    return std::nullopt;
  }
  BcryptKey kh{hkey};

  ULONG len = 0;
  if (BCryptDecrypt(kh.get(), const_cast<PUCHAR>(wrapped.data()),
                    static_cast<ULONG>(wrapped.size()), &oaep, nullptr, 0,
                    nullptr, 0, &len, BCRYPT_PAD_OAEP) < 0) {
    return std::nullopt;
  }
  if (len != kDataKeyLength) return std::nullopt;
  DataKey out{};
  ULONG written = 0;
  if (BCryptDecrypt(kh.get(), const_cast<PUCHAR>(wrapped.data()),
                    static_cast<ULONG>(wrapped.size()), &oaep, nullptr, 0,
                    out.data(), static_cast<ULONG>(out.size()), &written,
                    BCRYPT_PAD_OAEP) < 0) {
    return std::nullopt;
  }
  if (written != out.size()) return std::nullopt;
  return out;
}

}  // namespace secretkeeper::crypto
