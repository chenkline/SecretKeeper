#pragma once

// 机密心 - RSA 密钥 DER 编解码
//
// Windows CNG 不接受 PKCS#1 RSAPublicKey / PKCS#8 PrivateKeyInfo 的 DER 输入，
// 而容器格式规定磁盘上保存的正是这两种 DER。因此这里负责双向转换：
//   DER -> 大端整数 -> CNG BCRYPT_RSAKEY_BLOB
//   CNG blob -> 大端整数 -> DER
//
// 只支持本项目需要的最小子集，不实现通用 ASN.1，也不校验证书链。
// 解析一律严格：拒绝非最短长度编码、多余前导零与可选字段，
// 避免宽容解析把畸形文件伪装成有效输入。

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace secretkeeper::der {

// 大端无符号整数，已去除多余前导零（空数组表示 0）。
using BigInt = std::vector<std::uint8_t>;

struct RsaPublicKey {
  BigInt modulus;   // n
  BigInt exponent;  // e
};

struct RsaPrivateKey {
  BigInt modulus;          // n
  BigInt public_exponent;  // e
  BigInt private_exponent; // d
  BigInt prime1;           // p
  BigInt prime2;           // q
  BigInt exponent1;        // d mod (p-1)
  BigInt exponent2;        // d mod (q-1)
  BigInt coefficient;      // q^-1 mod p
};

// ---- 解析 ----
std::optional<RsaPublicKey> parse_pkcs1_public(const std::uint8_t* data,
                                                std::size_t size);
std::optional<RsaPrivateKey> parse_pkcs8_private(const std::uint8_t* data,
                                                 std::size_t size);

// ---- 序列化 ----
std::vector<std::uint8_t> write_pkcs1_public(const RsaPublicKey& key);
std::vector<std::uint8_t> write_pkcs8_private(const RsaPrivateKey& key);

// ---- 与 CNG blob 互转（CNG blob 使用小端序）----
// CNG 导出的私钥 blob 不含 d，此处从 p、q、e 还原并用 dp/dq/qInv 交叉校验。
std::vector<std::uint8_t> to_cng_public_blob(const RsaPublicKey& key);
std::vector<std::uint8_t> to_cng_private_blob(const RsaPrivateKey& key);
std::optional<RsaPublicKey> from_cng_public_blob(const std::uint8_t* data,
                                                  std::size_t size);
std::optional<RsaPrivateKey> from_cng_private_blob(const std::uint8_t* data,
                                                   std::size_t size);

// DER <-> CNG 私钥 blob 便捷封装
std::optional<std::vector<std::uint8_t>> pkcs8_to_cng_private_blob(
    const std::uint8_t* data, std::size_t size);
std::vector<std::uint8_t> cng_private_blob_to_pkcs8(
    const std::uint8_t* data, std::size_t size);

}  // namespace secretkeeper::der
