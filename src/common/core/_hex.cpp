#include "core/store.h"

namespace secretkeeper::store {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

// 只接受小写十六进制。索引库里的 ID 一律由本模块生成，因此大写输入属于
// 「非本模块产生的值」，按非法处理可以让大小写混写无法绕过主键唯一性。
int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

}  // namespace

bool bytes_from_hex(std::string_view hex, std::vector<std::uint8_t>* out) {
  if (hex.size() % 2 != 0) return false;
  std::vector<std::uint8_t> tmp;
  tmp.reserve(hex.size() / 2);
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    const int hi = nibble(hex[i]);
    const int lo = nibble(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    tmp.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
  }
  *out = std::move(tmp);
  return true;
}

std::string bytes_to_hex(std::span<const std::uint8_t> bytes) {
  std::string s;
  s.reserve(bytes.size() * 2);
  for (std::uint8_t b : bytes) {
    s.push_back(kHexDigits[b >> 4]);
    s.push_back(kHexDigits[b & 0x0F]);
  }
  return s;
}

bool id_from_hex(std::string_view hex, Id* out) {
  if (hex.size() != kHexCharsPerId) return false;
  Id tmp{};
  for (std::size_t i = 0; i < serialize::kIdLength; ++i) {
    const int hi = nibble(hex[i * 2]);
    const int lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    tmp[i] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  *out = tmp;
  return true;
}

bool id_to_hex(const Id& id, std::string* out) {
  out->clear();
  out->reserve(kHexCharsPerId);
  for (std::uint8_t b : id) {
    out->push_back(kHexDigits[b >> 4]);
    out->push_back(kHexDigits[b & 0x0F]);
  }
  return true;
}

}  // namespace secretkeeper::store
