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

#include "../src/crypto/crypto.h"
#include "../src/crypto/der.h"

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
                        std::size_t from) {
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

using namespace secretkeeper::crypto;
namespace der = secretkeeper::der;

}  // namespace

int main(int argc, char** argv) {
  // 用法：crypto_test <仓库根目录>
  const std::string root = (argc > 1) ? argv[1] : ".";
  const std::string vec = root + "/test-vectors/";

  // ------------------------------------------------------------- Argon2id
  std::printf("Argon2id (m=10240KiB t=3 p=1 out=32 v=19):\n");
  {
    const std::string text = read_file(vec + "kdf-argon2id.json");
    check(!text.empty(), "kdf-argon2id.json 可读");
    int n = 0;
    std::size_t pos = 0;
    for (int guard = 0; guard < 64; ++guard) {
      const std::size_t name_pos = find_field(text, "name", pos);
      if (name_pos == std::string::npos) break;
      const std::string name = json_string(text, "name", pos);
      const std::string pwd_hex = json_string(text, "passwordHex", pos);
      const std::string salt_hex = json_string(text, "saltHex", pos);
      const std::string expect = json_string(text, "expectedHex", pos);
      if (name.empty() || expect.empty()) break;
      pos = find_field(text, "expectedHex", pos) + 12;
      ++n;

      const auto pwd = from_hex(pwd_hex);
      const auto salt_b = from_hex(salt_hex);
      if (salt_b.size() != kSaltLength) {
        check(false, name + ": 盐长度应为 " + std::to_string(kSaltLength));
        continue;
      }
      Salt salt{};
      std::memcpy(salt.data(), salt_b.data(), kSaltLength);
      const Kek kek = derive_kek(std::span<const std::uint8_t>(pwd), salt);
      check(to_hex(kek.data(), kek.size()) == expect, name + ": KEK 与向量一致");
    }
    check(n == 6, "Argon2id 向量条数为 6");
    std::printf("  共 %d 条\n", n);
  }

  // ---------------------------------------------------------- AES-256-GCM
  std::printf("AES-256-GCM:\n");
  {
    const std::string text = read_file(vec + "aes-gcm.json");
    check(!text.empty(), "aes-gcm.json 可读");
    int n = 0;
    std::size_t pos = 0;
    for (int guard = 0; guard < 64; ++guard) {
      const std::string name = json_string(text, "name", pos);
      if (name.empty()) break;
      const std::string key_hex = json_string(text, "keyHex", pos);
      const std::string nonce_hex = json_string(text, "nonceHex", pos);
      const std::string aad_hex = json_string(text, "aadHex", pos);
      const std::string pt_hex = json_string(text, "plaintextHex", pos);
      const std::string ct_hex = json_string(text, "cipherHex", pos);
      const std::string tag_hex = json_string(text, "tagHex", pos);
      pos = find_field(text, "tagHex", pos) + 7;
      if (tag_hex.empty()) break;
      ++n;

      const auto key = from_hex(key_hex);
      const auto nonce_b = from_hex(nonce_hex);
      const auto aad = from_hex(aad_hex);
      const auto pt = from_hex(pt_hex);
      const auto ct = from_hex(ct_hex);
      const auto tag_b = from_hex(tag_hex);
      if (key.size() != 32 || nonce_b.size() != kNonceLength ||
          tag_b.size() != kTagLength) {
        check(false, name + ": 向量字段长度非法");
        continue;
      }
      Nonce nonce{};
      Tag tag{};
      std::memcpy(nonce.data(), nonce_b.data(), kNonceLength);
      std::memcpy(tag.data(), tag_b.data(), kTagLength);

      std::vector<std::uint8_t> got(pt.size() + 1);
      aes_gcm_encrypt(key.data(), key.size(), nonce, aad, pt, got.data(),
                      tag.data());
      check(std::memcmp(got.data(), ct.data(), ct.size()) == 0,
            name + ": 密文与向量一致");
      check(to_hex(tag.data(), kTagLength) == tag_hex,
            name + ": 认证标签与向量一致");

      std::vector<std::uint8_t> back(pt.size() + 1);
      const CryptoError err =
          aes_gcm_decrypt(key.data(), key.size(), nonce, aad, ct, tag,
                          back.data());
      check(err == CryptoError::kOk, name + ": 解密成功");
      check(std::memcmp(back.data(), pt.data(), pt.size()) == 0,
            name + ": 明文还原一致");
    }
    check(n == 5, "AES-GCM 向量条数为 5");
    std::printf("  共 %d 条\n", n);
  }

  // ----------------------------------------------------------- RSA-2048
  std::printf("RSA-2048 OAEP-SHA256:\n");
  if (!cng_has_asymmetric_support()) {
    std::printf(
        "  已跳过：本机 CNG 不提供非对称算法（RSA/ECC/DH）。\n"
        "  这是精简版或容器化 Windows 镜像的已知限制，\n"
        "  完整 RSA 覆盖由 GitHub Actions 的 windows-2022 runner 执行。\n");
  } else {
    const RsaKeyPair kp = generate_rsa2048();
    check(kp.public_der.size() > 0, "导出 PKCS#1 公钥 DER");
    check(kp.private_der.size() > 0, "导出 PKCS#8 私钥 DER");

    DataKey dk{};
    random_bytes(std::span<std::uint8_t>(dk.data(), dk.size()));
    const auto wrapped =
        rsa_oaep_encrypt(std::span<const std::uint8_t>(kp.public_der.data(),
                                                       kp.public_der.size()),
                         std::span<const std::uint8_t>(dk.data(), dk.size()));
    check(wrapped.has_value(), "生成密钥可封装数据密钥");
    if (wrapped) {
      check(wrapped->size() == kRsaModulusBytes,
            "封装结果长度为 " + std::to_string(kRsaModulusBytes));
      const auto back = rsa_oaep_decrypt(
          std::span<const std::uint8_t>(kp.private_der.data(),
                                         kp.private_der.size()),
          std::span<const std::uint8_t>(wrapped->data(), wrapped->size()));
      check(back.has_value(), "可解封装");
      check(back && *back == dk, "解封装还原出原数据密钥");

      // OAEP 含随机种子，两次封装必不同，但都要能解开
      const auto wrapped2 =
          rsa_oaep_encrypt(std::span<const std::uint8_t>(kp.public_der.data(),
                                                         kp.public_der.size()),
                           std::span<const std::uint8_t>(dk.data(),
                                                         dk.size()));
      check(wrapped2 && *wrapped2 != *wrapped,
            "两次封装结果不同（OAEP 随机种子生效）");
    }

    // 数据密钥长度超过 OAEP 上限（190 字节）必须失败而非静默截断
    std::vector<std::uint8_t> big(191, 0xAB);
    const auto bad = rsa_oaep_encrypt(
        std::span<const std::uint8_t>(kp.public_der.data(),
                                       kp.public_der.size()),
        std::span<const std::uint8_t>(big.data(), big.size()));
    check(!bad.has_value(), "超长明文被拒绝");
  }

  // ------------------------------------------------------ DER / CNG 互转
  std::printf("DER / CNG 互转（含私钥指数 d 还原）:\n");
  if (cng_has_asymmetric_support()) {
    const RsaKeyPair kp = generate_rsa2048();
    const auto pub = der::parse_pkcs1_public(kp.public_der.data(),
                                              kp.public_der.size());
    check(pub.has_value(), "PKCS#1 公钥可解析");
    if (pub) {
      const auto again = der::write_pkcs1_public(*pub);
      check(again.size() == kp.public_der.size() &&
                std::memcmp(again.data(), kp.public_der.data(), again.size()) == 0,
            "PKCS#1 公钥往返字节一致");
    }
    const auto priv = der::parse_pkcs8_private(kp.private_der.data(),
                                               kp.private_der.size());
    check(priv.has_value(), "PKCS#8 私钥可解析（d 已还原并校验）");
    if (priv) {
      const auto again = der::write_pkcs8_private(*priv);
      check(again.size() == kp.private_der.size() &&
                std::memcmp(again.data(), kp.private_der.data(), again.size()) == 0,
            "PKCS#8 私钥往返字节一致");
      const auto blob = der::pkcs8_to_cng_private_blob(kp.private_der.data(),
                                                       kp.private_der.size());
      check(blob.has_value(), "PKCS#8 可转回 CNG 私钥 blob");
    }

    // 畸形输入必须被拒绝
    const std::uint8_t junk[] = {0x30, 0x03, 0x02, 0x01, 0x00};
    check(!der::parse_pkcs1_public(junk, sizeof(junk)).has_value(),
          "截断的 PKCS#1 被拒绝");
    check(!der::parse_pkcs8_private(junk, sizeof(junk)).has_value(),
          "截断的 PKCS#8 被拒绝");
  }

  std::printf("\n%d 项检查，%d 项失败\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
