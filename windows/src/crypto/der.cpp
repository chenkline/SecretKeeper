// 机密心 - RSA 密钥 DER 编解码实现

#include "der.h"

#include "bignum.h"

#include <algorithm>
#include <cstring>
#include <numeric>

namespace bn = secretkeeper::bignum;

namespace secretkeeper::der {
namespace {

// ---- 严格的 DER 读取游标 ----
// 只接受最短长度编码与最小 INTEGER 编码；多余的前导零或非最短长度一律拒绝，
// 因为宽松解析会把畸形文件伪装成有效输入。

class Reader {
 public:
  Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  bool eof() const { return pos_ >= size_; }
  std::size_t remaining() const { return size_ - pos_; }

  bool read_byte(std::uint8_t& out) {
    if (pos_ >= size_) return false;
    out = data_[pos_++];
    return true;
  }

  bool read_tag(std::uint8_t& out) { return read_byte(out); }

  // 长度：短格式 <0x80，长格式 0x81..0x84。拒绝不定长(0x80)与非最短编码。
  bool read_length(std::size_t& out) {
    std::uint8_t first = 0;
    if (!read_byte(first)) return false;
    if (first < 0x80) {
      out = first;
      return true;
    }
    std::size_t count = first & 0x7F;
    if (count == 0 || count > 4) return false;  // 不定长或超出本实现范围
    if (remaining() < count) return false;
    std::size_t value = 0;
    for (std::size_t i = 0; i < count; ++i) {
      std::uint8_t b = 0;
      if (!read_byte(b)) return false;
      value = (value << 8) | b;
    }
    if (value < 0x80) return false;  // 非最短编码
    out = value;
    return true;
  }

  // 读取一个 TLV，返回内容区间；要求恰好读完 value_len 个字节。
  bool read_tlv(std::uint8_t& tag, const std::uint8_t*& body, std::size_t& body_len) {
    if (!read_tag(tag)) return false;
    std::size_t len = 0;
    if (!read_length(len)) return false;
    if (remaining() < len) return false;
    body = data_ + pos_;
    body_len = len;
    pos_ += len;
    return true;
  }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

// DER INTEGER 的内容区去掉前导 0x00 与首个符号位后得到规范大端整数。
// 要求：长度为 1..(modulus_bytes+1)，高位（符号位后）不得全为 0（避免负数），
// 且不得存在多余的前导零。
bool integer_to_bigint(const std::uint8_t* body, std::size_t len, BigInt& out) {
  if (len == 0) return false;
  if (body[0] & 0x80) return false;  // 负数，RSA 参数不会出现
  if (len > 1 && body[0] == 0x00 && (body[1] & 0x80) == 0) {
    return false;  // 多余的前导零，非规范 DER
  }
  std::size_t skip = (len > 1 && body[0] == 0x00) ? 1 : 0;
  out.assign(body + skip, body + len);
  return true;
}

// 大端整数 -> DER INTEGER 内容区：保证最高位字节非 0，否则补一个 0x00 符号位。
void write_integer_body(const BigInt& value, std::vector<std::uint8_t>& out) {
  std::size_t start = 0;
  while (start + 1 < value.size() && value[start] == 0x00) ++start;
  const bool need_pad = !value.empty() && (value[start] & 0x80) != 0;
  if (need_pad) out.push_back(0x00);
  out.insert(out.end(), value.begin() + static_cast<long>(start), value.end());
}

// 写一个 DER TLV：tag + 最短长度 + 内容。
void write_tlv(std::uint8_t tag, const std::vector<std::uint8_t>& body,
               std::vector<std::uint8_t>& out) {
  out.push_back(tag);
  std::size_t len = body.size();
  if (len < 0x80) {
    out.push_back(static_cast<std::uint8_t>(len));
  } else {
    std::vector<std::uint8_t> len_bytes;
    std::size_t v = len;
    while (v > 0) {
      len_bytes.push_back(static_cast<std::uint8_t>(v & 0xFF));
      v >>= 8;
    }
    out.push_back(static_cast<std::uint8_t>(0x80 | len_bytes.size()));
    for (auto it = len_bytes.rbegin(); it != len_bytes.rend(); ++it) {
      out.push_back(*it);
    }
  }
  out.insert(out.end(), body.begin(), body.end());
}

constexpr std::uint8_t kSeq = 0x30;
constexpr std::uint8_t kInt = 0x02;
constexpr std::uint8_t kBitString = 0x03;
constexpr std::uint8_t kOctetString = 0x04;
constexpr std::uint8_t kObjectId = 0x06;
constexpr std::uint8_t kZero = 0x00;

// rsaEncryption 的 OID 内容：1.2.840.113549.1.1.1
constexpr std::uint8_t kRsaOid[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D,
                                    0x01, 0x01, 0x01};

void append_oid(std::vector<std::uint8_t>& out) {
  write_tlv(kObjectId,
            std::vector<std::uint8_t>(std::begin(kRsaOid), std::end(kRsaOid)),
            out);
}

void append_integer(const BigInt& value, std::vector<std::uint8_t>& out) {
  std::vector<std::uint8_t> body;
  write_integer_body(value, body);
  write_tlv(kInt, body, out);
}

void append_null(std::vector<std::uint8_t>& out) {
  write_tlv(kZero, {}, out);
}

// ---- CNG blob 辅助 ----
// BCRYPT_RSAKEY_BLOB 头部：Magic(4) BitLength(4) cbPublicExp(4) cbModulus(4)
// cbPrime1(4) -> 公钥头共 24 字节，私钥头共 32 字节（含 cbPrime2）。
// 之后依次是 PublicExponent / Modulus / [Prime1 / Prime2 / Exponent1 / Exponent2 / Coefficient]，
// 全部小端序。

constexpr std::size_t kCngPublicHeader = 24;
constexpr std::size_t kCngPrivateHeader = 32;
constexpr std::uint32_t kCngMagicPublic = 0x31415352;  // RSA1
constexpr std::uint32_t kCngMagicPrivate = 0x32415352; // RSA2

void put32_le(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

std::uint32_t get32_le(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

void put_le(std::vector<std::uint8_t>& out, const BigInt& v) {
  for (auto it = v.rbegin(); it != v.rend(); ++it) out.push_back(*it);
}

// 小端字节序列 -> 规范大端整数；全零返回空（表示数值 0）。
BigInt from_le(const std::uint8_t* p, std::size_t len) {
  std::size_t start = len;
  while (start > 0 && p[start - 1] == 0x00) --start;
  BigInt out;
  out.reserve(start);
  for (std::size_t i = start; i > 0; --i) out.push_back(p[i - 1]);
  return out;
}

}  // namespace

// ---------------------------------------------------------------- PKCS#1 公钥
// RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER }

std::optional<RsaPublicKey> parse_pkcs1_public(const std::uint8_t* data,
                                               std::size_t size) {
  Reader r(data, size);
  std::uint8_t tag = 0;
  const std::uint8_t* body = nullptr;
  std::size_t body_len = 0;
  if (!r.read_tlv(tag, body, body_len) || tag != kSeq) return std::nullopt;
  if (!r.eof()) return std::nullopt;  // 尾部多余数据

  Reader inner(body, body_len);
  RsaPublicKey key;
  const std::uint8_t* field = nullptr;
  std::size_t field_len = 0;
  if (!inner.read_tlv(tag, field, field_len) || tag != kInt) return std::nullopt;
  if (!integer_to_bigint(field, field_len, key.modulus)) return std::nullopt;
  if (!inner.read_tlv(tag, field, field_len) || tag != kInt) return std::nullopt;
  if (!integer_to_bigint(field, field_len, key.exponent)) return std::nullopt;
  if (!inner.eof()) return std::nullopt;
  return key;
}

std::vector<std::uint8_t> write_pkcs1_public(const RsaPublicKey& key) {
  std::vector<std::uint8_t> body;
  append_integer(key.modulus, body);
  append_integer(key.exponent, body);
  std::vector<std::uint8_t> out;
  write_tlv(kSeq, body, out);
  return out;
}

// ------------------------------------------------------------ PKCS#8 私钥
// PrivateKeyInfo ::= SEQUENCE { version, AlgorithmIdentifier, OCTET STRING }
// OCTET STRING 内容为 RSAPrivateKey ::= SEQUENCE { version, n, e, d, p, q,
//                                                dp, dq, qInv }

std::optional<RsaPrivateKey> parse_pkcs8_private(const std::uint8_t* data,
                                                 std::size_t size) {
  Reader r(data, size);
  std::uint8_t tag = 0;
  const std::uint8_t* body = nullptr;
  std::size_t body_len = 0;
  if (!r.read_tlv(tag, body, body_len) || tag != kSeq) return std::nullopt;
  if (!r.eof()) return std::nullopt;

  Reader outer(body, body_len);
  const std::uint8_t* field = nullptr;
  std::size_t field_len = 0;

  // version = 0
  if (!outer.read_tlv(tag, field, field_len) || tag != kInt) return std::nullopt;
  if (field_len != 1 || field[0] != 0x00) return std::nullopt;

  // AlgorithmIdentifier: SEQUENCE { OID rsaEncryption, NULL }
  if (!outer.read_tlv(tag, field, field_len) || tag != kSeq) return std::nullopt;
  {
    Reader alg(field, field_len);
    const std::uint8_t* oid = nullptr;
    std::size_t oid_len = 0;
    if (!alg.read_tlv(tag, oid, oid_len) || tag != kObjectId) return std::nullopt;
    if (oid_len != sizeof(kRsaOid) ||
        std::memcmp(oid, kRsaOid, oid_len) != 0) {
      return std::nullopt;  // 非 RSA
    }
    if (!alg.read_tlv(tag, oid, oid_len) || tag != kZero) return std::nullopt;
    if (!alg.eof()) return std::nullopt;
  }

  // privateKey OCTET STRING
  if (!outer.read_tlv(tag, field, field_len) || tag != kOctetString) {
    return std::nullopt;
  }
  if (!outer.eof()) return std::nullopt;  // 不接受 attributes / publicKey 字段

  Reader inner(field, field_len);
  if (!inner.read_tlv(tag, field, field_len) || tag != kSeq) return std::nullopt;
  if (!inner.eof()) return std::nullopt;

  Reader nums(field, field_len);
  RsaPrivateKey key;
  const std::uint8_t* num = nullptr;
  std::size_t num_len = 0;
  // version = 0
  if (!nums.read_tlv(tag, num, num_len) || tag != kInt) return std::nullopt;
  if (num_len != 1 || num[0] != 0x00) return std::nullopt;

  BigInt* fields[] = {&key.modulus,      &key.public_exponent, &key.private_exponent,
                      &key.prime1,       &key.prime2,         &key.exponent1,
                      &key.exponent2,    &key.coefficient};
  for (BigInt* f : fields) {
    if (!nums.read_tlv(tag, num, num_len) || tag != kInt) return std::nullopt;
    if (!integer_to_bigint(num, num_len, *f)) return std::nullopt;
  }
  if (!nums.eof()) return std::nullopt;
  return key;
}

std::vector<std::uint8_t> write_pkcs8_private(const RsaPrivateKey& key) {
  // RSAPrivateKey
  std::vector<std::uint8_t> inner_body;
  append_integer(BigInt{0x00}, inner_body);  // version
  append_integer(key.modulus, inner_body);
  append_integer(key.public_exponent, inner_body);
  append_integer(key.private_exponent, inner_body);
  append_integer(key.prime1, inner_body);
  append_integer(key.prime2, inner_body);
  append_integer(key.exponent1, inner_body);
  append_integer(key.exponent2, inner_body);
  append_integer(key.coefficient, inner_body);

  std::vector<std::uint8_t> inner;
  write_tlv(kSeq, inner_body, inner);

  // AlgorithmIdentifier
  std::vector<std::uint8_t> alg_body;
  append_oid(alg_body);
  append_null(alg_body);
  std::vector<std::uint8_t> alg;
  write_tlv(kSeq, alg_body, alg);

  // PrivateKeyInfo
  std::vector<std::uint8_t> body;
  append_integer(BigInt{0x00}, body);
  body.insert(body.end(), alg.begin(), alg.end());
  write_tlv(kOctetString, inner, body);

  std::vector<std::uint8_t> out;
  write_tlv(kSeq, body, out);
  return out;
}

// ---------------------------------------------------------------- CNG 互转

std::vector<std::uint8_t> to_cng_public_blob(const RsaPublicKey& key) {
  std::vector<std::uint8_t> out;
  out.reserve(kCngPublicHeader + key.exponent.size() + key.modulus.size());
  put32_le(out, kCngMagicPublic);
  put32_le(out, static_cast<std::uint32_t>(key.modulus.size() * 8));
  put32_le(out, static_cast<std::uint32_t>(key.exponent.size()));
  put32_le(out, static_cast<std::uint32_t>(key.modulus.size()));
  put32_le(out, 0);  // cbPrime1
  put_le(out, key.exponent);
  put_le(out, key.modulus);
  return out;
}

std::vector<std::uint8_t> to_cng_private_blob(const RsaPrivateKey& key) {
  std::vector<std::uint8_t> out;
  put32_le(out, kCngMagicPrivate);
  put32_le(out, static_cast<std::uint32_t>(key.modulus.size() * 8));
  put32_le(out, static_cast<std::uint32_t>(key.public_exponent.size()));
  put32_le(out, static_cast<std::uint32_t>(key.modulus.size()));
  put32_le(out, static_cast<std::uint32_t>(key.prime1.size()));
  put32_le(out, static_cast<std::uint32_t>(key.prime2.size()));
  put_le(out, key.public_exponent);
  put_le(out, key.modulus);
  put_le(out, key.prime1);
  put_le(out, key.prime2);
  put_le(out, key.exponent1);
  put_le(out, key.exponent2);
  put_le(out, key.coefficient);
  return out;
}

std::optional<RsaPublicKey> from_cng_public_blob(const std::uint8_t* data,
                                                 std::size_t size) {
  if (size < kCngPublicHeader) return std::nullopt;
  if (get32_le(data) != kCngMagicPublic) return std::nullopt;
  std::uint32_t cb_exp = get32_le(data + 8);
  std::uint32_t cb_mod = get32_le(data + 12);
  if (size != kCngPublicHeader + cb_exp + cb_mod) return std::nullopt;
  if (cb_exp == 0 || cb_mod == 0) return std::nullopt;
  RsaPublicKey key;
  key.exponent = from_le(data + kCngPublicHeader, cb_exp);
  key.modulus = from_le(data + kCngPublicHeader + cb_exp, cb_mod);
  if (key.modulus.empty()) return std::nullopt;
  return key;
}


// ---------------------------------------------- CNG 私钥 blob -> PKCS#8
// CNG 的 RSA2 blob 不含私钥指数 d（CNG 内部按 CRT 缓存，只导出 p/q/dp/dq/qInv），
// 而 PKCS#8 必须含 d。这里用费马小定理还原：
//   lambda = lcm(p-1, q-1)
//   d      = e^-1 mod lambda
// 再用 CNG 一并给出的 dp / dq / qInv 交叉校验，任一项不符即判失败。
// 这样即使推导有误也绝不会写出错误的私钥。
std::optional<RsaPrivateKey> from_cng_private_blob(const std::uint8_t* data,
                                                   std::size_t size) {
  if (size < kCngPrivateHeader) return std::nullopt;
  if (get32_le(data) != kCngMagicPrivate) return std::nullopt;
  const std::uint32_t cb_exp = get32_le(data + 8);
  const std::uint32_t cb_mod = get32_le(data + 12);
  const std::uint32_t cb_p1 = get32_le(data + 16);
  const std::uint32_t cb_p2 = get32_le(data + 20);
  const std::size_t total =
      kCngPrivateHeader + cb_exp + cb_mod + 2 * cb_p1 + 4 * cb_p2;
  if (size != total) return std::nullopt;
  if (cb_exp == 0 || cb_mod == 0 || cb_p1 == 0 || cb_p2 == 0) {
    return std::nullopt;
  }

  std::size_t off = kCngPrivateHeader;
  auto take = [&](std::size_t n) {
    BigInt v = from_le(data + off, n);
    off += n;
    return v;
  };
  const BigInt e_bytes = take(cb_exp);
  const BigInt n_bytes = take(cb_mod);
  const BigInt p_bytes = take(cb_p1);
  const BigInt q_bytes = take(cb_p2);
  const BigInt dp_bytes = take(cb_p1);
  const BigInt dq_bytes = take(cb_p2);
  const BigInt qi_bytes = take(cb_p2);
  if (n_bytes.empty() || p_bytes.empty() || q_bytes.empty() ||
      e_bytes.empty()) {
    return std::nullopt;
  }

  using bn::Num;
  const Num e = bn::from_bytes_be(e_bytes.data(), e_bytes.size());
  const Num p = bn::from_bytes_be(p_bytes.data(), p_bytes.size());
  const Num q = bn::from_bytes_be(q_bytes.data(), q_bytes.size());

  const Num p1 = bn::subtract(p, Num{1});  // p - 1
  const Num q1 = bn::subtract(q, Num{1});  // q - 1

  // lambda = lcm(p-1, q-1) = (p-1)(q-1) / gcd(p-1, q-1)
  Num prod, rem;
  bn::divmod(bn::multiply(p1, q1), bn::gcd(p1, q1), prod, rem);
  if (!bn::is_zero(rem)) return std::nullopt;
  const Num lambda = prod;

  const auto d_opt = bn::mod_inverse(e, lambda);
  if (!d_opt) return std::nullopt;
  const Num d = *d_opt;
  if (bn::is_zero(d)) return std::nullopt;

  // 交叉校验：CNG 给出的 dp / dq / qInv 必须与我们推导的结果一致。
  const Num dp = bn::modulo(d, p1);
  const Num dq = bn::modulo(d, q1);
  const Num dp_expect = bn::from_bytes_be(dp_bytes.data(), dp_bytes.size());
  const Num dq_expect = bn::from_bytes_be(dq_bytes.data(), dq_bytes.size());
  const Num qi_expect = bn::from_bytes_be(qi_bytes.data(), qi_bytes.size());
  if (bn::compare(dp, dp_expect) != 0) return std::nullopt;
  if (bn::compare(dq, dq_expect) != 0) return std::nullopt;
  const auto qi = bn::mod_inverse(q, p);
  if (!qi || bn::compare(*qi, qi_expect) != 0) return std::nullopt;

  RsaPrivateKey key;
  key.modulus = n_bytes;
  key.public_exponent = e_bytes;
  key.prime1 = p_bytes;
  key.prime2 = q_bytes;
  key.exponent1 = bn::to_bytes_be(dp, p_bytes.size());
  key.exponent2 = bn::to_bytes_be(dq, q_bytes.size());
  key.coefficient = qi_bytes;
  key.private_exponent = bn::to_bytes_be(d, n_bytes.size());
  return key;
}

std::optional<std::vector<std::uint8_t>> pkcs8_to_cng_private_blob(
    const std::uint8_t* data, std::size_t size) {
  const auto key = parse_pkcs8_private(data, size);
  if (!key) return std::nullopt;
  return to_cng_private_blob(*key);
}

std::vector<std::uint8_t> cng_private_blob_to_pkcs8(const std::uint8_t* data,
                                                     std::size_t size) {
  const auto key = from_cng_private_blob(data, size);
  if (!key) return {};
  return write_pkcs8_private(*key);
}
}  // namespace secretkeeper::der
