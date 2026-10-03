#pragma once

// 机密心 - 容器格式读写
//
// 实现 docs/03-data/数据格式与存储设计.md 定义的字节级格式。
// 本文件是格式规范的唯一代码化表达，任何与文档不一致之处以文档为准。
//
// 解析原则（对应文档 §3.4）：
//   1. PayloadLen 必须与文件剩余长度精确一致，多余字节视为非法
//   2. 每个 TLV 长度不得超过剩余可用长度
//   3. 固定长度字段长度必须精确匹配
//   任一项不满足即返回明确错误码，绝不崩溃、绝不越界读取。

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "crypto.h"

namespace secretkeeper::container {

using crypto::Id;
using crypto::Nonce;
using crypto::Salt;
using crypto::Tag;

// ---- 格式常量 ----
inline constexpr std::array<std::uint8_t, 4> kMasterKeyMagic = {'S', 'M', 'K', '1'};
inline constexpr std::array<std::uint8_t, 4> kSecretMagic = {'S', 'S', 'C', '1'};
inline constexpr std::uint16_t kFormatVersion = 0x0001;
inline constexpr std::size_t kHeaderSize = 12;  // Magic(4)+Version(2)+Flags(2)+PayloadLen(4)

inline constexpr std::uint8_t kAlgRsa2048 = 1;  // 2 = SM2，v0.0.1 必须拒绝
inline constexpr std::uint8_t kEncAlgAesGcm = 1;

// TLV 长度前缀的字节数（uint32）。
inline constexpr std::size_t kTlvPrefixSize = 4;

// 任何单个 TLV 载荷、以及整个 payload 的合理上限，用于识别被篡改的长度字段。
//
// 这个界不是随手取的 1 MiB，而是从格式本身算出来的：
//   主密钥文件 payload 上界 ≈ mkAlg(1) + ID/盐/Nonce/Tag 等固定字段
//     + PKCS#8 DER 私钥（约 1.2 KB）+ PKCS#1 DER 公钥（约 294 B）
//     ≈ 1.7 KB（格式向量实测 1642 字节）
//   机密信息文件 payload 上界 ≈ 两个 16 字节 ID + 标题(<=150 字符)
//     + 256 字节 wrappedDk + Nonce/Tag + 密文(<=150 字符)
//     ≈ 700 字节（格式向量实测 368 字节）
// 取 4 KiB 已留出约 2.4 倍余量，同时足以把 9999 这类明显离谱的声明值
// 判为「长度字段非法」，而不是误当成「文件被截断」。
//
// 用途有二：拒绝被篡改的 PayloadLen，以及防止按 0xFFFFFFFF 之类
// 的畸形长度分配内存。
inline constexpr std::uint32_t kMaxPlausibleFileBytes = 4u * 1024u;

// 错误类别与 test-vectors/negative-cases.json 的 errorCategories 一一对应。
enum class ParseError {
  kOk = 0,
  kBadMagic,            // 文件类型不匹配
  kUnsupportedVersion,  // 版本号高于支持范围
  kTruncated,           // 数据截断
  kLengthMismatch,      // 长度字段与实际不符
  kUnknownAlgorithm,    // 未知算法标识（如 v0.0.1 遇到 SM2）
  kInvalidUtf8,         // 名称或标题不是合法 UTF-8
};

const char* to_string(ParseError e);

// ---- 主密钥文件 ----
// name 允许为空，仅用于显示，不参与加解密、不纳入 AAD。
struct MasterKeyFile {
  std::uint8_t mk_alg = kAlgRsa2048;
  Id master_key_id{};
  std::string name;  // UTF-8，可为空
  Salt salt{};       // 明文存储，这是跨端一致的前提
  std::uint32_t kdf_mem = 0;
  std::uint32_t kdf_iter = 0;
  std::uint32_t kdf_par = 0;
  std::uint8_t pub_key_enc_alg = kEncAlgAesGcm;
  Nonce pub_key_nonce{};
  Tag pub_key_tag{};
  std::vector<std::uint8_t> pub_key_cipher;
  std::uint8_t priv_key_enc_alg = kEncAlgAesGcm;
  Nonce priv_key_nonce{};
  Tag priv_key_tag{};
  std::vector<std::uint8_t> priv_key_cipher;
};

// ---- 机密信息文件 ----
struct SecretFile {
  Id secret_id{};      // 明文，作为 GCM AAD
  Id master_key_id{};  // 明文，列表页无需解密即可比对
  std::string title;   // UTF-8，可为空；不加密是明确的取舍
  std::vector<std::uint8_t> wrapped_dk;
  Nonce data_nonce{};
  Tag data_tag{};
  std::vector<std::uint8_t> data_cipher;
};

// ---- 序列化 ----
// 结果必须与解析前逐字节相同（见 container-roundtrip 向量）。
std::vector<std::uint8_t> serialize(const MasterKeyFile& f);
std::vector<std::uint8_t> serialize(const SecretFile& f);

// ---- 解析 ----
std::optional<MasterKeyFile> parse_master_key(std::span<const std::uint8_t> bytes,
                                              ParseError* error);
std::optional<SecretFile> parse_secret(std::span<const std::uint8_t> bytes,
                                       ParseError* error);

}  // namespace secretkeeper::container
