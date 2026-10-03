#pragma once

// 机密心 - Unicode 码点计数
//
// 限额单位是**字符（Unicode 码点）**，不是 UTF-16 单元，也不是 UTF-8 字节。
// 三者对同一段文本会给出不同数字：例如 U+1F600（😀）占 1 个码点，
// 但占 2 个 UTF-16 单元、4 个 UTF-8 字节。
//
// 五端必须给出同一个判定结果，因此这里只按 UTF-8 序列结构解析：
// 跳过续字节（10xxxxxx）即得到码点数。不做 Unicode 规范化，也不校验
// 码点是否合法——那属于容器层的 invalid_utf8 职责。

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace secretkeeper::text {

// 按 Unicode 码点计数。非法 UTF-8 字节按每个字节一个码点处理，
// 这样不会因为一个坏字节就少算或崩溃。
std::size_t count_code_points(std::string_view utf8);

inline constexpr std::size_t kMaxSecretLength = 150;  // 单条机密信息 150 字符

bool is_valid_utf8(std::string_view s);

}  // namespace secretkeeper::text
