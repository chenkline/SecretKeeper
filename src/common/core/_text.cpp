#include "core/_text.h"

namespace secretkeeper::text {

bool is_valid_utf8(std::string_view s) {
  std::size_t i = 0;
  const std::size_t n = s.size();
  while (i < n) {
    const auto b0 = static_cast<std::uint8_t>(s[i]);
    std::size_t len = 0;
    std::uint32_t cp = 0;
    if (b0 < 0x80) {
      i += 1;
      continue;
    } else if ((b0 & 0xE0) == 0xC0) {
      len = 2;
      cp = b0 & 0x1Fu;
    } else if ((b0 & 0xF0) == 0xE0) {
      len = 3;
      cp = b0 & 0x0Fu;
    } else if ((b0 & 0xF8) == 0xF0) {
      len = 4;
      cp = b0 & 0x07u;
    } else {
      return false;  // 0x80-0xBF 续字节开头，或 0xF8-0xFF 非法
    }
    if (i + len > n) return false;
    for (std::size_t k = 1; k < len; ++k) {
      const auto bk = static_cast<std::uint8_t>(s[i + k]);
      if ((bk & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (bk & 0x3Fu);
    }
    // 拒绝过长编码与代理区码点，保证码点数与真值一致。
    if (len == 2 && cp < 0x80) return false;
    if (len == 3 && cp < 0x800) return false;
    if (len == 4 && cp < 0x10000) return false;
    if (cp > 0x10FFFF) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    i += len;
  }
  return true;
}

std::size_t count_code_points(std::string_view utf8) {
  std::size_t count = 0;
  for (char c : utf8) {
    // 续字节是 10xxxxxx，不开启一个新码点。
    if ((static_cast<std::uint8_t>(c) & 0xC0) != 0x80) ++count;
  }
  return count;
}

}  // namespace secretkeeper::text
