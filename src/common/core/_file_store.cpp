#include "core/store.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace secretkeeper::store {
namespace {

constexpr char kDirKeys[] = "keys";
constexpr char kDirSecrets[] = "secrets";

// 统一错误出口。std::error_code 携带平台原生错误码，Linux/macOS 下即 errno。
void set_fs_error(std::string* error, const char* what, const std::error_code& ec) {
  if (error == nullptr) return;
  *error = std::string(what) + " (errno=" + std::to_string(ec.value()) + ")";
}

bool is_valid_rel_path(std::string_view rel) {
  // rel_path 全部由本模块拼装，但仍拒绝绝对路径与 .. 以防调用方传入
  // 恶意相对路径把文件写到数据目录之外。
  if (rel.empty() || rel.size() > 260) return false;
  if (rel[0] == '/' || rel[0] == '\\' || rel[1] == ':') return false;
  for (std::size_t i = 0; i + 1 < rel.size(); ++i) {
    if (rel[i] == '.' && rel[i + 1] == '.') return false;
  }
  return true;
}

}  // namespace

bool FileStore::ensure_dir(const std::string& path, std::string* error) const {
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path), ec);
  if (!ec) return true;
  // 已存在时 create_directories 不报错，但若存在的是普通文件则需显式拒绝。
  if (ec == std::errc::file_exists) {
    if (std::filesystem::is_directory(std::filesystem::path(path), ec) && !ec) return true;
    set_fs_error(error, "path exists but is not a directory", ec);
    return false;
  }
  set_fs_error(error, "create_directories failed", ec);
  return false;
}

bool FileStore::ensure_layout(std::string* error) const {
  if (data_dir_.empty()) {
    if (error != nullptr) *error = "data dir not set";
    return false;
  }
  if (!ensure_dir(data_dir_, error)) return false;
  if (!ensure_dir(data_dir_ + "/" + kDirKeys, error)) return false;
  return ensure_dir(data_dir_ + "/" + kDirSecrets, error);
}

std::string FileStore::master_key_rel_path(std::string_view master_key_id_hex) {
  return std::string("keys/") + std::string(master_key_id_hex) + ".smk";
}

std::string FileStore::secret_rel_path(std::string_view secret_id_hex) {
  return std::string("secrets/") + std::string(secret_id_hex) + ".ssc";
}

bool FileStore::master_key_path(std::string_view master_key_id_hex, std::string* out) const {
  if (master_key_id_hex.size() != kHexCharsPerId || data_dir_.empty()) return false;
  out->assign(data_dir_);
  out->push_back('/');
  out->append(master_key_rel_path(master_key_id_hex));
  return true;
}

bool FileStore::secret_path(std::string_view secret_id_hex, std::string* out) const {
  if (secret_id_hex.size() != kHexCharsPerId || data_dir_.empty()) return false;
  out->assign(data_dir_);
  out->push_back('/');
  out->append(secret_rel_path(secret_id_hex));
  return true;
}

bool FileStore::write_atomic(const std::string& rel_path, std::span<const std::uint8_t> bytes,
                             std::string* error) const {
  if (data_dir_.empty()) {
    if (error != nullptr) *error = "data dir not set";
    return false;
  }
  if (!is_valid_rel_path(rel_path)) {
    if (error != nullptr) *error = "invalid relative path";
    return false;
  }
  const std::string full = data_dir_ + "/" + rel_path;

  // 同目录下的临时文件，保证改名是同一卷内的原子操作。
  const std::string tmp = full + ".tmp";
  {
    std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
    if (!os) {
      if (error != nullptr) *error = "open temp file failed";
      return false;
    }
    if (!bytes.empty()) {
      os.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    }
    os.flush();
    if (!os) {
      os.close();
      std::error_code ec;
      std::filesystem::remove(tmp, ec);
      if (error != nullptr) *error = "write failed";
      return false;
    }
  }
  // ofstream 析构即关闭；此处显式 rename 保证同卷原子替换。
  std::error_code ec;
  std::filesystem::rename(tmp, full, ec);
  if (ec) {
    // POSIX 的 rename 会覆盖目标；Windows 的 filesystem::rename 在目标存在时失败，
    // 需先删除再改名。
    std::error_code rm_ec;
    std::filesystem::remove(full, rm_ec);
    ec.clear();
    std::filesystem::rename(tmp, full, ec);
  }
  if (ec) {
    std::error_code rm_ec;
    std::filesystem::remove(tmp, rm_ec);
    set_fs_error(error, "rename failed", ec);
    return false;
  }
  return true;
}

bool FileStore::read(const std::string& rel_path, std::vector<std::uint8_t>* out,
                     std::string* error) const {
  if (!is_valid_rel_path(rel_path)) {
    if (error != nullptr) *error = "invalid relative path";
    return false;
  }
  std::ifstream is(data_dir_ + "/" + rel_path, std::ios::binary);
  if (!is) {
    if (error != nullptr) *error = "open failed";
    return false;
  }
  // 容器文件上限 4 GiB，超过即视为异常输入。
  constexpr std::uintmax_t kMaxFileSize = 0xFFFFFFFFull;
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(std::filesystem::path(data_dir_) / rel_path, ec);
  if (ec || size > kMaxFileSize) {
    if (error != nullptr) *error = "file size failed or too large";
    return false;
  }
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
  if (size > 0) {
    is.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));
    if (!is) {
      if (error != nullptr) *error = "read failed";
      return false;
    }
  }
  *out = std::move(buf);
  return true;
}

bool FileStore::remove(const std::string& rel_path, std::string* error) const {
  if (!is_valid_rel_path(rel_path)) {
    if (error != nullptr) *error = "invalid relative path";
    return false;
  }
  std::error_code ec;
  std::filesystem::remove(data_dir_ + "/" + rel_path, ec);
  // 文件不存在视为成功（与旧实现对 ERROR_FILE_NOT_FOUND 的处理一致）。
  if (ec && ec != std::errc::no_such_file_or_directory) {
    set_fs_error(error, "remove failed", ec);
    return false;
  }
  return true;
}

}  // namespace secretkeeper::store
