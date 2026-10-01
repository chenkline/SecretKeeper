#pragma once

// 机密心 - 十六进制 ID 与字节 ID 的互转
//
// 索引库以 32 位小写十六进制字符串作主键，数据文件与 GCM AAD 用原始 16 字节。
// 两者的转换必须严格：奇数长度、非法字符一律拒绝，不做静默截断或补零。

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../crypto/crypto.h"

namespace secretkeeper::store {

using crypto::Id;

inline constexpr std::size_t kHexCharsPerId = crypto::kIdLength * 2;  // 32

// 成功返回 true；失败时 out 不被修改。
bool id_from_hex(std::string_view hex, Id* out);
bool id_to_hex(const Id& id, std::string* out);

// 任意长度的字节/十六进制互转，供测试与调试使用。
bool bytes_from_hex(std::string_view hex, std::vector<std::uint8_t>* out);
std::string bytes_to_hex(std::span<const std::uint8_t> bytes);

}  // namespace secretkeeper::store
