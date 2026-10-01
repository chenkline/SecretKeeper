// 机密心 - 极简大整数运算实现
//
// 约定：小端 32 位字数组，规范化（末尾无零字），空数组表示 0。
// 所有函数都不修改调用方对象。
//
// 关于 divmod：使用「移位-约减」二进制长除法，正确但较慢。
// 只在生成主密钥时调用一次（模逆 + 一次乘法），耗时在百毫秒量级，
// 不值得为它引入更复杂、更易出错的快速除法。

#include "bignum.h"

#include <algorithm>

namespace secretkeeper::bignum {
namespace {

void trim(Num& v) {
  while (!v.empty() && v.back() == 0) v.pop_back();
}

// a -= b，要求 a >= b
void sub_assign(Num& a, const Num& b) {
  std::int64_t borrow = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    std::int64_t cur = static_cast<std::int64_t>(a[i]) - borrow;
    if (i < b.size()) cur -= static_cast<std::int64_t>(b[i]);
    if (cur < 0) {
      cur += (std::int64_t{1} << 32);
      borrow = 1;
    } else {
      borrow = 0;
    }
    a[i] = static_cast<std::uint32_t>(cur);
  }
  trim(a);
}

// a <<= 1
Num shift_left_1(const Num& a) {
  Num out = a;
  out.insert(out.begin(), 0);
  trim(out);
  return out;
}

// a += bit（bit 为 0 或 1），用于长除法中把下一位并入余数
Num set_bit(Num v, bool bit) {
  if (!bit) return v;
  if (v.empty()) {
    v.push_back(1);
  } else {
    v[0] |= 1u;
  }
  return v;
}

std::size_t bit_length(const Num& a) {
  if (a.empty()) return 0;
  std::size_t bits = (a.size() - 1) * 32;
  std::uint32_t top = a.back();
  while (top != 0) {
    ++bits;
    top >>= 1;
  }
  return bits;
}

bool test_bit(const Num& a, std::size_t bit) {
  const std::size_t word = bit / 32;
  if (word >= a.size()) return false;
  return ((a[word] >> (bit % 32)) & 1u) != 0;
}

}  // namespace

Num from_bytes_be(const std::uint8_t* data, std::size_t size) {
  Num v;
  v.reserve((size + 3) / 4 + 1);
  std::size_t i = size;
  while (i > 0) {
    const std::size_t take = (i >= 4) ? 4 : i;
    std::uint32_t word = 0;
    for (std::size_t k = 0; k < take; ++k) {
      word |= static_cast<std::uint32_t>(data[i - 1 - k]) << (8 * k);
    }
    v.push_back(word);
    i -= take;
  }
  trim(v);
  return v;
}

std::vector<std::uint8_t> to_bytes_be(const Num& value, std::size_t min_len) {
  const std::size_t byte_len = std::max(value.size() * 4, min_len);
  std::vector<std::uint8_t> out(byte_len, 0);
  for (std::size_t i = 0; i < value.size(); ++i) {
    const std::uint32_t word = value[i];
    const std::size_t base = byte_len - (i + 1) * 4;
    for (std::size_t k = 0; k < 4; ++k) {
      out[base + 3 - k] = static_cast<std::uint8_t>((word >> (8 * k)) & 0xFF);
    }
  }
  return out;
}

bool is_zero(const Num& a) { return a.empty(); }

bool is_one(const Num& a) { return a.size() == 1 && a[0] == 1; }

int compare(const Num& a, const Num& b) {
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  for (std::size_t i = a.size(); i > 0; --i) {
    if (a[i - 1] != b[i - 1]) return a[i - 1] < b[i - 1] ? -1 : 1;
  }
  return 0;
}

Num add(const Num& a, const Num& b) {
  Num out;
  out.reserve(std::max(a.size(), b.size()) + 1);
  std::uint64_t carry = 0;
  const std::size_t n = std::max(a.size(), b.size());
  for (std::size_t i = 0; i < n || carry != 0; ++i) {
    std::uint64_t sum = carry;
    if (i < a.size()) sum += a[i];
    if (i < b.size()) sum += b[i];
    out.push_back(static_cast<std::uint32_t>(sum & 0xFFFFFFFFu));
    carry = sum >> 32;
  }
  trim(out);
  return out;
}

Num subtract(const Num& a, const Num& b) {
  Num out = a;
  sub_assign(out, b);
  return out;
}

Num multiply(const Num& a, const Num& b) {
  if (a.empty() || b.empty()) return {};
  Num out(a.size() + b.size(), 0);
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] == 0) continue;
    std::uint64_t carry = 0;
    for (std::size_t j = 0; j < b.size(); ++j) {
      const std::uint64_t cur = out[i + j] +
                                static_cast<std::uint64_t>(a[i]) * b[j] + carry;
      out[i + j] = static_cast<std::uint32_t>(cur & 0xFFFFFFFFu);
      carry = cur >> 32;
    }
    std::size_t k = i + b.size();
    while (carry != 0) {
      const std::uint64_t cur = out[k] + carry;
      out[k] = static_cast<std::uint32_t>(cur & 0xFFFFFFFFu);
      carry = cur >> 32;
      ++k;
    }
  }
  trim(out);
  return out;
}

// 二进制长除法：q = a / b，r = a % b
void divmod(const Num& a, const Num& b, Num& q, Num& r) {
  q.clear();
  r.clear();
  if (b.empty()) return;  // 除以零，调用方保证不会发生
  if (compare(a, b) < 0) {
    r = a;
    return;
  }
  // 从最高位到最低位做「移位 + 试减」
  Num quotient(a.size(), 0);
  Num rem;
  const std::size_t a_bits = bit_length(a);
  for (std::size_t i = a_bits; i > 0; --i) {
    rem = set_bit(shift_left_1(rem), test_bit(a, i - 1));
    if (compare(rem, b) >= 0) {
      sub_assign(rem, b);
      quotient[(i - 1) / 32] |= 1u << ((i - 1) % 32);
    }
  }
  q = quotient;
  trim(q);
}

Num modulo(const Num& a, const Num& m) {
  if (m.empty()) return a;
  Num q, r;
  divmod(a, m, q, r);
  return r;
}

Num gcd(const Num& a, const Num& b) {
  Num x = a, y = b;
  while (!is_zero(y)) {
    Num q, r;
    divmod(x, y, q, r);
    x = y;
    y = r;
  }
  return x;
}

// 扩展欧几里得求模逆：要求 gcd(a, m) == 1
//
// 只跟踪 a 的系数，且始终把它化到 [0, m) 区间内，因此不需要带符号数。
// 更新式 (old_s - q*s) mod m 用「相加后至多减一次 m」实现，
// 不能用「反复加 m 直到够大」——商 q 可能极大，那样会退化成天文数字的循环。
std::optional<Num> mod_inverse(const Num& a, const Num& m) {
  if (a.empty() || m.empty()) return std::nullopt;

  Num old_r = m, r = a;
  Num old_s;        // 系数 0
  Num s = Num{1};   // 系数 1

  while (!is_zero(r)) {
    Num q, rem;
    divmod(old_r, r, q, rem);

    // 余数序列前进：(old_r, r) -> (r, rem)
    old_r = r;
    r = rem;

    // 系数序列同步：old_s' = s, s' = (old_s - q*s) mod m
    Num next_s = old_s;
    if (!q.empty()) {
      const Num qs = modulo(multiply(q, s), m);
      if (compare(old_s, qs) >= 0) {
        sub_assign(next_s, qs);
      } else {
        next_s = add(old_s, m);
        sub_assign(next_s, qs);  // 此处 old_s + m >= qs 已成立，结果 < m
      }
    }
    old_s = s;
    s = next_s;
  }

  if (!is_one(old_r)) return std::nullopt;  // gcd(a, m) != 1，逆元不存在
  return modulo(old_s, m);
}
}  // namespace secretkeeper::bignum
