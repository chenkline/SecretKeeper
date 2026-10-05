// 机密心 - 存储层自检：索引库 + 数据文件 + 十六进制 ID 互转。
//
// 与密码学层同结构：直接 main() + 返回码，不依赖测试框架，
// 输出全 ASCII，避免 CI 上非 UTF-8 代码页让 job 变红。

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "core/store.h"

namespace store = secretkeeper::store;

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

// 生成一个稳定的小写 32 位十六进制 ID。
std::string make_id(char seed) {
  std::string s;
  s.reserve(32);
  for (int i = 0; i < 32; ++i) {
    s.push_back(static_cast<char>("0123456789abcdef"[(seed + i) % 16]));
  }
  return s;
}

// 15 条互不相同的机密信息 ID：高位用索引保证唯一。
std::string secret_id_at(int index) {
  static const char* k = "0123456789abcdef";
  std::string s;
  s.reserve(32);
  for (int i = 0; i < 32; ++i) {
    const int v = (i < 8) ? (index & 0xFF) : 0;
    s.push_back(k[v % 16]);
  }
  s[1] = k[(index >> 4) & 0xF];
  return s;
}

void test_hex() {
  std::printf("Hex ID conversion\n");

  const std::string hex = "00112233445566778899aabbccddeeff";
  store::Id id{};
  check(store::id_from_hex(hex, &id), "id_from_hex accepts 32 lowercase hex chars");
  check(id[0] == 0x00 && id[15] == 0xff, "id bytes match hex");
  std::string back;
  check(store::id_to_hex(id, &back), "id_to_hex succeeds");
  check(back == hex, "hex roundtrip is lossless");

  // 拒绝非法输入：长度、大写、非十六进制。
  store::Id tmp{};
  check(!store::id_from_hex("00112233", &tmp), "reject short hex");
  check(!store::id_from_hex(hex + "00", &tmp), "reject long hex");
  check(!store::id_from_hex("00112233445566778899AABBCCDDEEFF", &tmp),
        "reject uppercase hex");
  check(!store::id_from_hex("00112233445566778899aabbccddeefg", &tmp),
        "reject non-hex char");
  check(!store::id_from_hex("", &tmp), "reject empty hex");

  std::vector<std::uint8_t> bytes;
  check(store::bytes_from_hex("0a0b0c", &bytes), "bytes_from_hex odd-length-free input");
  check(bytes.size() == 3 && bytes[0] == 0x0a && bytes[2] == 0x0c, "bytes decode correctly");
  check(store::bytes_to_hex(bytes) == "0a0b0c", "bytes_to_hex roundtrips");
  check(!store::bytes_from_hex("abc", &bytes), "reject odd-length hex");
}

void test_index_db(const std::filesystem::path& dir) {
  std::printf("Index DB (SQLite)\n");

  std::error_code mk_ec;
  std::filesystem::create_directories(dir, mk_ec);

  store::IndexDb db;
  std::string err;
  const std::string db_path = (dir / "secret.db").string();
  check(db.open(db_path) == store::StoreError::kOk, "open creates schema");
  check(db.is_open(), "db reports open");

  // 限额：2 个主密钥。
  store::MasterKeyRow mk;
  mk.master_key_id = make_id(0);
  mk.name = "";
  mk.file_path = store::FileStore::master_key_rel_path(mk.master_key_id);
  mk.is_default = true;
  check(db.upsert_master_key(mk) == store::StoreError::kOk, "insert master key 1");
  check(db.default_master_key_id().value_or("") == mk.master_key_id,
        "default master key recorded");

  store::MasterKeyRow mk2 = mk;
  mk2.master_key_id = make_id(3);
  mk2.file_path = store::FileStore::master_key_rel_path(mk2.master_key_id);
  mk2.is_default = false;
  check(db.upsert_master_key(mk2) == store::StoreError::kOk, "insert master key 2");

  // 限额判定已上移到 service 层：本层对第三把主密钥照收不误。
  store::MasterKeyRow mk3 = mk;
  mk3.master_key_id = make_id(7);
  mk3.file_path = store::FileStore::master_key_rel_path(mk3.master_key_id);
  check(db.upsert_master_key(mk3) == store::StoreError::kOk,
        "third master key accepted at store layer");
  check(db.master_key_count() == 3, "master key count is 3");
  check(db.delete_master_key(mk3.master_key_id) == store::StoreError::kOk,
        "remove third master key again");
  check(db.master_key_count() == 2, "master key count back to 2");

  // 更新既有记录不得触发限额。
  store::MasterKeyRow updated = mk;
  updated.name = "renamed";
  check(db.upsert_master_key(updated) == store::StoreError::kOk,
        "update existing master key not blocked by quota");
  check(db.find_master_key(mk.master_key_id)->name == "renamed", "name update persisted");
  check(db.find_master_key(mk.master_key_id)->created_at > 0, "created_at is set");
  check(db.find_master_key(mk.master_key_id)->created_at <=
            db.find_master_key(mk.master_key_id)->updated_at,
        "created_at not after updated_at");
  check(db.find_master_key(mk2.master_key_id)->created_at > 0,
        "second master key has created_at");

  // 非法参数。
  store::MasterKeyRow bad = mk;
  bad.master_key_id = "XYZ";
  check(db.upsert_master_key(bad) == store::StoreError::kInvalidArgument,
        "reject non-hex master key id");

  // 默认主密钥唯一性。
  check(db.set_default_master_key(mk2.master_key_id) == store::StoreError::kOk,
        "switch default master key");
  check(db.default_master_key_id().value_or("") == mk2.master_key_id,
        "default switched");
  std::size_t defaults = 0;
  for (const auto& r : db.list_master_keys()) {
    if (r.is_default) ++defaults;
  }
  check(defaults == 1, "exactly one default master key");

  // 机密信息限额：15 条。
  for (std::size_t i = 0; i < 15; ++i) {
    store::SecretRow s;
    s.secret_id = secret_id_at(static_cast<int>(i) + 1);
    s.master_key_id = mk.master_key_id;
    s.title = (i % 2 == 0) ? "" : ("title" + std::to_string(i));
    s.file_path = store::FileStore::secret_rel_path(s.secret_id);
    check(db.upsert_secret(s) == store::StoreError::kOk,
          "insert secret " + std::to_string(i));
  }
  check(db.secret_count() == 15, "secret count is 15");
  store::SecretRow s16;
  s16.secret_id = secret_id_at(16);
  s16.master_key_id = mk.master_key_id;
  s16.file_path = store::FileStore::secret_rel_path(s16.secret_id);
  // 限额判定已上移到 service 层：本层对第 16 条照收不误。
  check(db.upsert_secret(s16) == store::StoreError::kOk,
        "16th secret accepted at store layer");
  check(db.secret_count() == 16, "secret count is 16");

  // 悬空引用合法：secrets.master_key_id 不建外键。
  // 先腾出一个位置，塞一条挂在 mk2 下的机密信息。
  check(db.delete_secret(secret_id_at(1)) == store::StoreError::kOk, "free one secret slot");
  check(db.secret_count() == 15, "one slot freed");
  store::SecretRow owned_by_mk2;
  owned_by_mk2.secret_id = secret_id_at(30);
  owned_by_mk2.master_key_id = mk2.master_key_id;
  owned_by_mk2.file_path = store::FileStore::secret_rel_path(owned_by_mk2.secret_id);
  check(db.upsert_secret(owned_by_mk2) == store::StoreError::kOk,
        "insert secret owned by mk2");
  check(db.secret_count() == 16, "back to 16 secrets");

  std::size_t dangling = 0;
  for (const auto& s : db.list_secrets()) {
    if (!db.find_master_key(s.master_key_id)) ++dangling;
  }
  check(dangling == 0, "all secrets currently resolve");

  // 删除主密钥不得连带删除机密信息。
  const std::size_t before = db.secret_count();
  check(db.delete_master_key(mk2.master_key_id) == store::StoreError::kOk,
        "delete master key 2");
  check(db.secret_count() == before, "secrets survive master key deletion");
  std::size_t dangling_after = 0;
  for (const auto& s : db.list_secrets()) {
    if (!db.find_master_key(s.master_key_id)) ++dangling_after;
  }
  check(dangling_after == 1, "exactly the orphaned secret dangles (yellow question mark case)");

  check(db.delete_master_key(mk2.master_key_id) == store::StoreError::kNotFound,
        "deleting missing master key reports not_found");
  check(db.delete_secret(secret_id_at(30)) == store::StoreError::kOk, "delete orphaned secret");
  check(db.delete_secret(secret_id_at(30)) == store::StoreError::kNotFound,
        "deleting missing secret reports not_found");

  // app_meta 往返。
  check(db.set_meta("lock_policy", "300000") == store::StoreError::kOk, "set meta");
  check(db.meta("lock_policy").value_or("") == "300000", "meta roundtrip");
  check(!db.meta("nonexistent").has_value(), "missing meta returns nullopt");

  // 删除默认主密钥后默认落到剩余记录。
  check(db.default_master_key_id().value_or("") == mk.master_key_id,
        "default reassigned after deleting previous default");
  db.close();
  check(!db.is_open(), "close clears handle");
}

void test_file_store(const std::filesystem::path& dir) {
  std::printf("Data file store\n");

  const std::filesystem::path data_dir = dir / "data";
  std::error_code mk_ec;
  std::filesystem::create_directories(data_dir, mk_ec);
  store::FileStore fs(data_dir.string());
  std::string err;
  check(fs.ensure_layout(&err), "ensure_layout creates dirs");
  check(std::filesystem::exists(data_dir / "keys"), "keys dir exists");
  check(std::filesystem::exists(data_dir / "secrets"), "secrets dir exists");

  const std::string id_hex = make_id(2);
  check(store::FileStore::master_key_rel_path(id_hex) == "keys/" + id_hex + ".smk",
        "master key rel path layout");
  check(store::FileStore::secret_rel_path(id_hex) == "secrets/" + id_hex + ".ssc",
        "secret rel path layout");

  // 写入 -> 读取 -> 逐字节一致。
  const std::vector<std::uint8_t> payload = {0x53, 0x4D, 0x4B, 0x31, 0x00, 0xFF, 0x10, 0x7B};
  const std::string rel = store::FileStore::master_key_rel_path(id_hex);
  check(fs.write_atomic(rel, payload, &err), "write_atomic succeeds");
  std::vector<std::uint8_t> back;
  check(fs.read(rel, &back, &err), "read succeeds");
  check(back == payload, "file bytes roundtrip losslessly");

  // 覆盖写。
  const std::vector<std::uint8_t> payload2 = {0x01, 0x02};
  check(fs.write_atomic(rel, payload2, &err), "overwrite succeeds");
  check(fs.read(rel, &back, &err) && back == payload2, "overwrite replaces content");

  // 拒绝路径穿越。
  check(!fs.write_atomic("../escape.smk", payload, &err), "reject parent-dir traversal");
  check(!fs.write_atomic("keys/../../escape.smk", payload, &err),
        "reject embedded traversal");
  check(!fs.write_atomic("", payload, &err), "reject empty rel path");

  // 不存在的文件读取应失败而非返回空。
  check(!fs.read("keys/" + make_id(9) + ".smk", &back, &err), "read of missing file fails");

  check(fs.remove(rel, &err), "remove succeeds");
  check(!fs.read(rel, &back, &err), "removed file no longer readable");
  check(fs.remove(rel, &err), "removing missing file is idempotent");

  // 空文件往返。
  check(fs.write_atomic("secrets/empty.ssc", {}, &err), "write empty payload");
  check(fs.read("secrets/empty.ssc", &back, &err) && back.empty(), "empty file roundtrip");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string root = (argc > 1) ? argv[1] : ".";

  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "secretkeeper_store_test";
  std::error_code ec;
  std::filesystem::remove_all(tmp, ec);

  std::printf("Storage layer self-check\n");

  test_hex();
  test_index_db(tmp / "db");
  test_file_store(tmp);

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
