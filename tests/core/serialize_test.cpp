// 机密心 - 密码学层自检：直接加载 test-vectors/ 下的黄金向量。
//
// 这是五端一致性校验的最小内核：任一端与向量不符即 CI 失败。
// 不依赖任何测试框架，直接 main() + 返回码，便于在任意构建系统里跑，
// 也让五端可以用同样的结构实现各自的校验入口。

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <span>
#include <string>
#include <vector>

#include "core/serialize.h"

namespace serialize = secretkeeper::serialize;

// RSA-2048 模长：wrappedDk 是一个完整的 OAEP 密文块，长度因此固定。
inline constexpr std::size_t kWrappedDkBytes = 256;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL  %s\n", what.c_str());
  }
}

std::vector<std::uint8_t> from_hex(const std::string& s) {
  std::vector<std::uint8_t> out;
  out.reserve(s.size() / 2);
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
    const int hi = nib(s[i]);
    const int lo = nib(s[i + 1]);
    if (hi < 0 || lo < 0) return {};
    out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
  }
  return out;
}

std::string to_hex(const std::uint8_t* p, std::size_t n) {
  static const char* k = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (std::size_t i = 0; i < n; ++i) {
    s.push_back(k[p[i] >> 4]);
    s.push_back(k[p[i] & 0xF]);
  }
  return s;
}

std::string read_file(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return {};
  std::string out;
  char buf[8192];
  std::size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

// 极简 JSON 读取：只支持本项目向量文件的扁平结构，避免各端测试引入 JSON 依赖。
// 五端的实现语言不同，但都按同样的字段名与同样的口径读取。
std::size_t find_field(const std::string& text, const std::string& key,
                       std::size_t from) {
  return text.find("\"" + key + "\"", from);
}

std::string json_string(const std::string& text, const std::string& key,
                        std::size_t from = 0) {
  std::size_t pos = find_field(text, key, from);
  if (pos == std::string::npos) return {};
  pos = text.find(':', pos);
  if (pos == std::string::npos) return {};
  ++pos;
  while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' ||
                                text[pos] == '\r' || text[pos] == '\t')) {
    ++pos;
  }
  if (pos >= text.size() || text[pos] != '"') return {};
  ++pos;
  std::string out;
  while (pos < text.size() && text[pos] != '"') {
    if (text[pos] == '\\' && pos + 1 < text.size()) {
      ++pos;
      switch (text[pos]) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        default: out.push_back(text[pos]); break;
      }
    } else {
      out.push_back(text[pos]);
    }
    ++pos;
  }
  return out;
}



}  // namespace

int main(int argc, char** argv) {
  // 用法：serialize_test <仓库根目录>
  const std::string root = (argc > 1) ? argv[1] : ".";
  const std::string vec = root + "/test-vectors/";

  // ---------------------------------------------------- 容器格式往返
  std::printf("Container roundtrip (SMK1 / SSC1)\n");
  {
    const std::string text = read_file(vec + "container-roundtrip.json");
    check(!text.empty(), "container-roundtrip.json readable");

    // --- 主密钥文件 ---
    const std::string mk_hex = json_string(text, "fileHex");
    check(!mk_hex.empty(), "has master key fileHex");
    const auto mk_bytes = from_hex(mk_hex);
    check(mk_bytes.size() > serialize::kHeaderSize, "master key file size sane");

    serialize::ParseError err = serialize::ParseError::kOk;
    const auto mk = serialize::parse_master_key(mk_bytes, &err);
    check(mk.has_value(), std::string("master key file parsed: ") +
                                serialize::to_string(err));
    if (mk) {
      check(mk->mk_alg == 1, "mkAlg == 1 (RSA-2048)");
      check(to_hex(mk->master_key_id.data(), secretkeeper::serialize::kIdLength) ==
                json_string(text, "masterKeyIdHex"),
            "masterKeyId matches vector");
      // 名称是 UTF-8 中文，与格式向量比对时按字节比较，
      // 避免测试自身受控制台代码页影响。
      static const unsigned char kWantName[] = {
          0xE6, 0x88, 0x91, 0xE7, 0x9A, 0x84, 0xE4, 0xB8, 0xBB,
          0xE5, 0xAF, 0x86, 0xE9, 0x92, 0xA5};
      check(mk->name.size() == sizeof(kWantName) &&
                std::memcmp(mk->name.data(), kWantName, sizeof(kWantName)) == 0,
            "name matches vector");
      check(to_hex(mk->salt.data(), secretkeeper::serialize::kSaltLength) ==
                json_string(text, "saltHex"),
            "salt matches vector");
      check(mk->kdf_mem == 10240 && mk->kdf_iter == 3 && mk->kdf_par == 1,
            "KDF params match vector");
      check(to_hex(mk->pub_key_nonce.data(), secretkeeper::serialize::kNonceLength) ==
                json_string(text, "pubKeyNonceHex"),
            "pubKeyNonce matches vector");
      check(to_hex(mk->priv_key_nonce.data(), secretkeeper::serialize::kNonceLength) ==
                json_string(text, "privKeyNonceHex"),
            "privKeyNonce matches vector");
      check(mk->pub_key_nonce != mk->priv_key_nonce,
            "pub/priv nonces differ (reuse leaks keys)");
      // 往返必须逐字节一致
      const auto again = serialize::serialize(*mk);
      check(again.size() == mk_bytes.size() &&
                std::memcmp(again.data(), mk_bytes.data(), again.size()) == 0,
            "master key roundtrip is byte-identical");
    }

    // --- 机密信息文件 ---
    const std::size_t sec_pos = text.find("secretFile");
    const std::string sec_hex = json_string(text, "fileHex", sec_pos);
    check(!sec_hex.empty(), "has secret fileHex");
    const auto sec_bytes = from_hex(sec_hex);
    serialize::ParseError serr = serialize::ParseError::kOk;
    const auto sf = serialize::parse_secret(sec_bytes, &serr);
    check(sf.has_value(), std::string("secret file parsed: ") +
                                serialize::to_string(serr));
    if (sf) {
      check(to_hex(sf->secret_id.data(), secretkeeper::serialize::kIdLength) ==
                json_string(text, "secretIdHex", sec_pos),
            "secretId matches vector");
      check(to_hex(sf->master_key_id.data(), secretkeeper::serialize::kIdLength) ==
                json_string(text, "masterKeyIdHex", sec_pos),
            "masterKeyId matches vector");
      static const unsigned char kWantTitle[] = {
          0xE6, 0xA0, 0x87, 0xE9, 0xA2, 0x98};
      check(sf->title.size() == sizeof(kWantTitle) &&
                std::memcmp(sf->title.data(), kWantTitle, sizeof(kWantTitle)) == 0,
            "title matches vector");
      check(sf->wrapped_dk.size() == kWrappedDkBytes,
            "wrappedDk length == 256");
      check(to_hex(sf->data_nonce.data(), secretkeeper::serialize::kNonceLength) ==
                json_string(text, "dataNonceHex", sec_pos),
            "dataNonce matches vector");
      const auto again = serialize::serialize(*sf);
      check(again.size() == sec_bytes.size() &&
                std::memcmp(again.data(), sec_bytes.data(), again.size()) == 0,
            "secret roundtrip is byte-identical");
    }
  }

  // ---------------------------------------------------- 负例：畸形输入
  std::printf("Container negative cases\n");
  {
    const std::string text = read_file(vec + "negative-cases.json");
    check(!text.empty(), "negative-cases.json readable");
    int n = 0;
    std::size_t pos = 0;
    for (int guard = 0; guard < 64; ++guard) {
      const std::string name = json_string(text, "name", pos);
      if (name.empty()) break;
      const std::string input_hex = json_string(text, "inputHex", pos);
      const std::string expect = json_string(text, "expectedError", pos);
      // 越界跳转：确保 pos 前进
      const std::size_t after = text.find("expectedError", pos);
      if (after == std::string::npos) break;
      pos = after + 13;
      ++n;
      if (input_hex.empty()) continue;  // GCM 类向量没有容器输入

      const auto bytes = from_hex(input_hex);
      serialize::ParseError e1 = serialize::ParseError::kOk;
      serialize::ParseError e2 = serialize::ParseError::kOk;
      const auto as_master = serialize::parse_master_key(bytes, &e1);
      const auto as_secret = serialize::parse_secret(bytes, &e2);
      check(!as_master.has_value() || !as_secret.has_value(),
            name + ": must be rejected");
      // 只要有一条路径报出期望的错误类别即算通过
      const bool matched =
          (!as_master && std::string(serialize::to_string(e1)) == expect) ||
          (!as_secret && std::string(serialize::to_string(e2)) == expect);
      check(matched, name + ": error category should be " + expect + " (got " +
                        serialize::to_string(e1) + " / " +
                        serialize::to_string(e2) + ")");
    }
    check(n >= 10, "negative case count >= 10");
    std::printf("  %d vectors\n", n);
  }
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
