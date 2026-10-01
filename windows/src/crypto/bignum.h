#pragma once

// 机密心 - 极简大整数运算
//
// 唯一用途：Windows CNG 导出的 RSA 私钥 blob（RSA2）不含私钥指数 d，
// 而容器格式规定磁盘上保存 PKCS#8 DER，PKCS#8 必须含 d。
// 因此这里用扩展欧几里得从 CNG 给出的 p、q、e 还原 d，再用 CNG 一并给出的
// dp / dq / qInv 交叉校验，校验不过直接判失败。
//
// 实现取向：宁慢勿错。乘法用 schoolbook，约减除法用二进制长除法，
// 不使用任何未经充分验证的快速算法。本文件只在生成主密钥时调用一次
// （每次主密钥生成一次，非每次加解密），耗时在百毫秒量级，完全可接受。

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace secretkeeper::bignum {

// 小端 32 位字数组表示，规范化（末尾无零字）；空数组表示 0。
using Num = std::vector<std::uint32_t>;

Num from_bytes_be(const std::uint8_t* data, std::size_t size);
std::vector<std::uint8_t> to_bytes_be(const Num& value, std::size_t min_len);

bool is_zero(const Num& a);
bool is_one(const Num& a);

// -1 / 0 / 1
int compare(const Num& a, const Num& b);

Num add(const Num& a, const Num& b);
// 要求 a >= b
Num subtract(const Num& a, const Num& b);
Num multiply(const Num& a, const Num& b);

// a = q * b + r（截断除法，要求 b != 0）
void divmod(const Num& a, const Num& b, Num& q, Num& r);
// 要求 m != 0；a < m 时原样返回
Num modulo(const Num& a, const Num& m);

Num gcd(const Num& a, const Num& b);

// a 在模 m 下的乘法逆元；gcd(a, m) != 1 时返回 nullopt
std::optional<Num> mod_inverse(const Num& a, const Num& m);

}  // namespace secretkeeper::bignum
