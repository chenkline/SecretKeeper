// 机密心 - 容器格式读写实现
//
// 全部整数一律大端序；变长字段一律 uint32 长度前缀 + 载荷。
// 解析器对任何畸形输入都返回明确错误码，不做"尽力猜测"。

#include "core/serialize.h"

#include <cstring>

namespace secretkeeper::serialize {
namespace {

// 顺序读取游标：任何越界读取都直接判定失败，绝不返回部分结果。
class Reader {
 public:
  Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  std::size_t remaining() const { return size_ - pos_; }
  bool eof() const { return pos_ >= size_; }

  bool read_u8(std::uint8_t& out) {
    if (remaining() < 1) return false;
    out = data_[pos_++];
    return true;
  }

  bool read_u16(std::uint16_t& out) {
    if (remaining() < 2) return false;
    out = static_cast<std::uint16_t>((data_[pos_] << 8) | data_[pos_ + 1]);
    pos_ += 2;
    return true;
  }

  bool read_u32(std::uint32_t& out) {
    if (remaining() < 4) return false;
    out = (static_cast<std::uint32_t>(data_[pos_]) << 24) |
          (static_cast<std::uint32_t>(data_[pos_ + 1]) << 16) |
          (static_cast<std::uint32_t>(data_[pos_ + 2]) << 8) |
          static_cast<std::uint32_t>(data_[pos_ + 3]);
    pos_ += 4;
    return true;
  }

  // TLV 读取结果。区分两种失败，因为它们对应不同的错误类别：
  //   kOk            成功
  //   kNoPrefix      连 4 字节长度前缀都没读到（文件在此之前就断了）
  //   kImplausible   长度前缀声明的长度大到任何合法文件都不可能有 -> 长度字段被篡改
  //   kShort         长度前缀可信，但剩余字节不足 -> 文件内容缺失
  enum class TlvStatus { kOk, kNoPrefix, kImplausible, kShort };

  TlvStatus read_tlv(std::vector<std::uint8_t>& out) {
    const std::size_t before = remaining();
    if (before < kTlvPrefixSize) return TlvStatus::kNoPrefix;
    std::uint32_t len = 0;
    if (!read_u32(len)) return TlvStatus::kNoPrefix;
    // 长度字段自身的可信度检查。kMaxPlausibleFileBytes 远大于任何
    // 合法的 v0.0.1 文件，因此超过它一定是长度字段被篡改，
    // 而不是「文件恰好少写了一些」。这一检查同时防止越界分配。
    if (len > kMaxPlausibleFileBytes) return TlvStatus::kImplausible;
    if (remaining() < len) return TlvStatus::kShort;
    out.assign(data_ + pos_, data_ + pos_ + len);
    pos_ += len;
    return TlvStatus::kOk;
  }

  // TLV 状态 -> 对应的错误类别。
  static ParseError tlv_error(TlvStatus s) {
    switch (s) {
      case TlvStatus::kOk: return ParseError::kOk;
      case TlvStatus::kNoPrefix: return ParseError::kTruncated;
      case TlvStatus::kImplausible: return ParseError::kLengthMismatch;
      case TlvStatus::kShort: return ParseError::kTruncated;
    }
    return ParseError::kLengthMismatch;
  }

  // 已消费的 payload 字节数。
  std::size_t consumed() const { return pos_; }

  // 剩余可用字节数。
  std::size_t avail() const { return remaining(); }

  // 固定长度字段，长度必须精确匹配。
  bool read_exact(std::uint8_t* out, std::size_t len) {
    if (remaining() < len) return false;
    std::memcpy(out, data_ + pos_, len);
    pos_ += len;
    return true;
  }

  // 读取 TLV 并要求长度精确等于 expect。固定长度字段（16 字节 ID、
  // 12 字节 Nonce、16 字节 Tag）必须用它：长度不符直接判 length_mismatch，
  // 不能接受「读前 N 字节、忽略多余」。
  typename Reader::TlvStatus read_tlv_exact(std::uint8_t* out,
                                           std::size_t expect) {
    if (remaining() < kTlvPrefixSize) return TlvStatus::kNoPrefix;
    std::uint32_t len = 0;
    if (!read_u32(len)) return TlvStatus::kNoPrefix;
    if (len != expect) return TlvStatus::kImplausible;
    if (remaining() < expect) return TlvStatus::kShort;
    if (!read_exact(out, expect)) return TlvStatus::kShort;
    return TlvStatus::kOk;
  }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

void put_u8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v >> 8));
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void put_tlv(std::vector<std::uint8_t>& out, const std::uint8_t* data,
             std::size_t len) {
  put_u32(out, static_cast<std::uint32_t>(len));
  out.insert(out.end(), data, data + len);
}

void put_tlv(std::vector<std::uint8_t>& out, const std::string& s) {
  put_u32(out, static_cast<std::uint32_t>(s.size()));
  out.insert(out.end(), s.begin(), s.end());
}

void put_tlv(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& v) {
  put_tlv(out, v.data(), v.size());
}

template <std::size_t N>
void put_tlv_array(std::vector<std::uint8_t>& out,
                   const std::array<std::uint8_t, N>& a) {
  put_tlv(out, a.data(), N);
}

void put_header(std::vector<std::uint8_t>& out,
                const std::array<std::uint8_t, 4>& magic,
                std::size_t payload_len) {
  out.insert(out.end(), magic.begin(), magic.end());
  put_u16(out, kFormatVersion);
  put_u16(out, 0);  // Flags：v0.0.1 恒为 0
  put_u32(out, static_cast<std::uint32_t>(payload_len));
}

// 校验容器头：Magic、Version、PayloadLen 三者都必须精确匹配。
// 阶段 1：只校验头部固定部分与「文件是否短到连一个 TLV 长度前缀都凑不齐」。
// 刻意不在这里比对 PayloadLen 的完整一致性——
// unknown-algorithm 必须在长度问题之前报出，顺序颠倒会让调用方
// 只看到「长度不对」而不知道真正原因是用了不支持的算法。
bool check_header_prefix(std::span<const std::uint8_t> bytes,
                         const std::array<std::uint8_t, 4>& expect_magic,
                         std::uint32_t& payload_len_out, ParseError* error) {
  auto fail = [&](ParseError e) {
    if (error) *error = e;
    return false;
  };
  if (bytes.size() < kHeaderSize) return fail(ParseError::kTruncated);
  if (std::memcmp(bytes.data(), expect_magic.data(), expect_magic.size()) != 0) {
    return fail(ParseError::kBadMagic);
  }
  Reader r(bytes.data() + 4, bytes.size() - 4);
  std::uint16_t version = 0;
  std::uint16_t flags = 0;
  if (!r.read_u16(version) || !r.read_u16(flags) ||
      !r.read_u32(payload_len_out)) {
    return fail(ParseError::kTruncated);
  }
  if (version > kFormatVersion) return fail(ParseError::kUnsupportedVersion);
  if (version < kFormatVersion) return fail(ParseError::kUnsupportedVersion);
  if (flags != 0) return fail(ParseError::kLengthMismatch);
  // PayloadLen 超过格式本身的物理上界，只能是被篡改的长度字段。
  // 先判这一条，才能把「声明 9999 字节」与「声明 366 字节但文件只有 8 字节」
  // 区分为 length_mismatch 与 truncated——两者都表现为"实际短于声明"，
  // 但根因完全不同。
  if (payload_len_out > kMaxPlausibleFileBytes) {
    return fail(ParseError::kLengthMismatch);
  }
  // 不在这里做「payload 是否够读一个 TLV」的判断。
  // 主密钥文件的第 1 个字节是 mkAlg 而非 TLV，若在头部就要求
  // payload 至少 4 字节，合法的「仅含 mkAlg 的极短文件」会被误判为截断，
  // 反而读不到那个正要被拒绝的算法标识。长度是否够，由 Reader 按
  // 具体字段逐个判断——只有它知道某个字段该不该出现在这个位置。
  if (error) *error = ParseError::kOk;
  return true;
}

// 阶段 2：PayloadLen 不得小于实际长度。
//
//   实际 > 声明   尾部有多余字节 -> length_mismatch（这里就能确定）
//   实际 < 声明   继续解析，由 TLV 层判定原因：
//                    TLV 长度前缀都读不到 / 前缀声明超出剩余 -> truncated
//                    前缀声明的长度本身离谱（超过合理上限） -> length_mismatch
//                  只有 TLV 层能区分这两种情况，头部层看不到。
bool check_payload_length(std::span<const std::uint8_t> bytes,
                         std::uint32_t payload_len, ParseError* error) {
  if (bytes.size() - kHeaderSize > payload_len) {
    if (error) *error = ParseError::kLengthMismatch;
    return false;
  }
  return true;
}

// 严格 UTF-8 校验：拒绝过长编码、代理区码点与超出 Unicode 范围的码点。
bool is_valid_utf8(const std::string& s) {
  const auto* p = reinterpret_cast<const unsigned char*>(s.data());
  std::size_t i = 0;
  const std::size_t n = s.size();
  while (i < n) {
    const unsigned char c = p[i];
    if (c < 0x80) {
      ++i;
    } else if ((c & 0xE0) == 0xC0) {
      if (i + 1 >= n || (p[i + 1] & 0xC0) != 0x80) return false;
      const unsigned cp = ((c & 0x1Fu) << 6) | (p[i + 1] & 0x3Fu);
      if (cp < 0x80) return false;  // 过长编码
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      if (i + 2 >= n || (p[i + 1] & 0xC0) != 0x80 || (p[i + 2] & 0xC0) != 0x80) {
        return false;
      }
      const unsigned cp = ((c & 0x0Fu) << 12) | ((p[i + 1] & 0x3Fu) << 6) |
                          (p[i + 2] & 0x3Fu);
      if (cp < 0x800) return false;                    // 过长编码
      if (cp >= 0xD800 && cp <= 0xDFFF) return false;  // 代理区码点
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      if (i + 3 >= n || (p[i + 1] & 0xC0) != 0x80 || (p[i + 2] & 0xC0) != 0x80 ||
          (p[i + 3] & 0xC0) != 0x80) {
        return false;
      }
      const unsigned cp = ((c & 0x07u) << 18) | ((p[i + 1] & 0x3Fu) << 12) |
                          ((p[i + 2] & 0x3Fu) << 6) | (p[i + 3] & 0x3Fu);
      if (cp < 0x10000 || cp > 0x10FFFF) return false;
      i += 4;
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

const char* to_string(ParseError e) {
  switch (e) {
    case ParseError::kOk: return "ok";
    case ParseError::kBadMagic: return "bad_magic";
    case ParseError::kUnsupportedVersion: return "unsupported_version";
    case ParseError::kTruncated: return "truncated";
    case ParseError::kLengthMismatch: return "length_mismatch";
    case ParseError::kUnknownAlgorithm: return "unknown_algorithm";
    case ParseError::kInvalidUtf8: return "invalid_utf8";
  }
  return "unknown";
}

// ------------------------------------------------------------- 主密钥文件

std::vector<std::uint8_t> serialize(const MasterKeyFile& f) {
  std::vector<std::uint8_t> p;
  p.reserve(256);
  put_u8(p, f.mk_alg);
  put_tlv_array(p, f.master_key_id);
  put_tlv(p, f.name);
  put_tlv_array(p, f.salt);
  put_u32(p, f.kdf_mem);
  put_u32(p, f.kdf_iter);
  put_u32(p, f.kdf_par);
  put_u8(p, f.pub_key_enc_alg);
  put_tlv_array(p, f.pub_key_nonce);
  put_tlv_array(p, f.pub_key_tag);
  put_tlv(p, f.pub_key_cipher);
  put_u8(p, f.priv_key_enc_alg);
  put_tlv_array(p, f.priv_key_nonce);
  put_tlv_array(p, f.priv_key_tag);
  put_tlv(p, f.priv_key_cipher);

  std::vector<std::uint8_t> out;
  out.reserve(kHeaderSize + p.size());
  put_header(out, kMasterKeyMagic, p.size());
  out.insert(out.end(), p.begin(), p.end());
  return out;
}

std::optional<MasterKeyFile> parse_master_key(std::span<const std::uint8_t> bytes,
                                              ParseError* error) {
  std::uint32_t payload_len = 0;
  if (!check_header_prefix(bytes, kMasterKeyMagic, payload_len, error)) {
    return std::nullopt;
  }
  auto fail = [&](ParseError e) {
    if (error) *error = e;
    return std::optional<MasterKeyFile>{};
  };

  Reader r(bytes.data() + kHeaderSize, bytes.size() - kHeaderSize);
  MasterKeyFile f;
  std::vector<std::uint8_t> name_bytes;

  // mkAlg 是裸字节而非 TLV，可能因为 payload 为空而读不到。
  if (r.avail() == 0) return fail(ParseError::kTruncated);
  if (!r.read_u8(f.mk_alg)) return fail(ParseError::kTruncated);
  // v0.0.1 只支持 RSA-2048。SM2(2) 等标识必须明确拒绝，
  // 绝不能按 RSA 继续处理——那是把未知算法当已知算法用的典型错误。
  // 这个检查刻意早于长度一致性检查：文件用了不支持的算法时，
  // 那才是调用方最需要知道的原因，长度问题只是附带现象。
  if (f.mk_alg != kAlgRsa2048) return fail(ParseError::kUnknownAlgorithm);

  // 算法标识校验通过后再判断长度一致性。
  if (!check_payload_length(bytes, payload_len, error)) return std::nullopt;

  // 统一的 TLV 读取辅助：失败时按状态给出对应的错误类别。
  auto read_tlv = [&](std::vector<std::uint8_t>& out) {
    const auto st = r.read_tlv(out);
    if (st != Reader::TlvStatus::kOk) fail(Reader::tlv_error(st));
    return st == Reader::TlvStatus::kOk;
  };
  auto read_tlv_exact = [&](std::uint8_t* out, std::size_t expect) {
    const auto st = r.read_tlv_exact(out, expect);
    if (st != Reader::TlvStatus::kOk) fail(Reader::tlv_error(st));
    return st == Reader::TlvStatus::kOk;
  };

  if (!read_tlv_exact(f.master_key_id.data(), kIdLength)) {
    return std::nullopt;
  }
  if (!read_tlv(name_bytes)) return std::nullopt;
  if (!is_valid_utf8(std::string(name_bytes.begin(), name_bytes.end()))) {
    return fail(ParseError::kInvalidUtf8);
  }
  f.name.assign(name_bytes.begin(), name_bytes.end());

  // 直接读进 std::array。不要用中间 vector 承接固定长度字段：
  // 空 vector 的 data() 为 nullptr，会让写入越界。
  if (!read_tlv_exact(f.salt.data(), kSaltLength)) {
    return std::nullopt;
  }

  if (!r.read_u32(f.kdf_mem) || !r.read_u32(f.kdf_iter) ||
      !r.read_u32(f.kdf_par)) {
    return fail(ParseError::kTruncated);
  }

  if (!r.read_u8(f.pub_key_enc_alg)) return fail(ParseError::kTruncated);
  if (f.pub_key_enc_alg != kEncAlgAesGcm) {
    return fail(ParseError::kUnknownAlgorithm);
  }
  if (!read_tlv_exact(f.pub_key_nonce.data(), kNonceLength) ||
      !read_tlv_exact(f.pub_key_tag.data(), kTagLength) ||
      !read_tlv(f.pub_key_cipher)) {
    return std::nullopt;
  }

  if (!r.read_u8(f.priv_key_enc_alg)) return fail(ParseError::kTruncated);
  if (f.priv_key_enc_alg != kEncAlgAesGcm) {
    return fail(ParseError::kUnknownAlgorithm);
  }
  if (!read_tlv_exact(f.priv_key_nonce.data(), kNonceLength) ||
      !read_tlv_exact(f.priv_key_tag.data(), kTagLength) ||
      !read_tlv(f.priv_key_cipher)) {
    return std::nullopt;
  }

  // 公私钥 nonce 必须不同。相同则两密文异或即可恢复两明文异或，
  // 加密彻底失效，因此这属于必须拒绝的非法文件而非可容忍的异常。
  if (f.pub_key_nonce == f.priv_key_nonce) {
    return fail(ParseError::kLengthMismatch);
  }

  // 解析成功后必须正好读完 Payload，不允许有未消费的字节。
  if (!r.eof()) return fail(ParseError::kLengthMismatch);
  if (error) *error = ParseError::kOk;
  return f;
}

// ------------------------------------------------------------ 机密信息文件

std::vector<std::uint8_t> serialize(const SecretFile& f) {
  std::vector<std::uint8_t> p;
  p.reserve(512);
  put_tlv_array(p, f.secret_id);
  put_tlv_array(p, f.master_key_id);
  put_tlv(p, f.title);
  put_tlv(p, f.wrapped_dk);
  put_tlv_array(p, f.data_nonce);
  put_tlv_array(p, f.data_tag);
  put_tlv(p, f.data_cipher);

  std::vector<std::uint8_t> out;
  out.reserve(kHeaderSize + p.size());
  put_header(out, kSecretMagic, p.size());
  out.insert(out.end(), p.begin(), p.end());
  return out;
}

std::optional<SecretFile> parse_secret(std::span<const std::uint8_t> bytes,
                                       ParseError* error) {
  std::uint32_t payload_len = 0;
  if (!check_header_prefix(bytes, kSecretMagic, payload_len, error)) {
    return std::nullopt;
  }
  auto fail = [&](ParseError e) {
    if (error) *error = e;
    return std::optional<SecretFile>{};
  };
  // 机密信息文件没有算法标识字段，长度校验紧接头部校验。
  if (!check_payload_length(bytes, payload_len, error)) return std::nullopt;
  Reader r(bytes.data() + kHeaderSize, bytes.size() - kHeaderSize);
  SecretFile f;
  std::vector<std::uint8_t> title_bytes;

  // 统一的 TLV 读取辅助：失败时按状态给出对应的错误类别。
  auto read_tlv = [&](std::vector<std::uint8_t>& out) {
    const auto st = r.read_tlv(out);
    if (st != Reader::TlvStatus::kOk) fail(Reader::tlv_error(st));
    return st == Reader::TlvStatus::kOk;
  };
  auto read_tlv_exact = [&](std::uint8_t* out, std::size_t expect) {
    const auto st = r.read_tlv_exact(out, expect);
    if (st != Reader::TlvStatus::kOk) fail(Reader::tlv_error(st));
    return st == Reader::TlvStatus::kOk;
  };



  if (!read_tlv_exact(f.secret_id.data(), kIdLength) ||
      !read_tlv_exact(f.master_key_id.data(), kIdLength)) {
    return std::nullopt;
  }
  if (!read_tlv(title_bytes)) return std::nullopt;
  if (!is_valid_utf8(std::string(title_bytes.begin(), title_bytes.end()))) {
    return fail(ParseError::kInvalidUtf8);
  }
  f.title.assign(title_bytes.begin(), title_bytes.end());

  if (!read_tlv(f.wrapped_dk) ||
      !read_tlv_exact(f.data_nonce.data(), kNonceLength) ||
      !read_tlv_exact(f.data_tag.data(), kTagLength) ||
      !read_tlv(f.data_cipher)) {
    return std::nullopt;
  }
  if (!r.eof()) return fail(ParseError::kLengthMismatch);
  if (error) *error = ParseError::kOk;
  return f;
}

}  // namespace secretkeeper::serialize
