#include "file_store.h"

#include <windows.h>

#include <filesystem>

namespace secretkeeper::store {
namespace {

constexpr wchar_t kDirKeys[] = L"keys";
constexpr wchar_t kDirSecrets[] = L"secrets";

// UTF-8 -> UTF-16 转换。路径来自本模块内部生成，但 data_dir 来自系统，
// 仍走正规 API 转换，不做字节级拼接。
bool to_wide(std::string_view s, std::wstring* out) {
  if (s.empty()) return false;
  const int need = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (need <= 0) return false;
  out->resize(static_cast<std::size_t>(need));
  const int got = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      out->data(), need);
  return got == need;
}

void set_win_error(std::string* error, const char* what) {
  if (error == nullptr) return;
  *error = std::string(what) + " (win32=" + std::to_string(GetLastError()) + ")";
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
  std::wstring wide;
  if (!to_wide(path, &wide)) {
    set_win_error(error, "path utf8->utf16 failed");
    return false;
  }
  if (CreateDirectoryW(wide.c_str(), nullptr)) return true;
  const DWORD err = GetLastError();
  if (err == ERROR_ALREADY_EXISTS) {
    const DWORD attrs = GetFileAttributesW(wide.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return true;
    set_win_error(error, "path exists but is not a directory");
    return false;
  }
  set_win_error(error, "CreateDirectory failed");
  return false;
}

bool FileStore::ensure_layout(std::string* error) const {
  if (data_dir_.empty()) {
    if (error != nullptr) *error = "data dir not set";
    return false;
  }
  if (!ensure_dir(data_dir_, error)) return false;
  if (!ensure_dir(data_dir_ + "/" + std::filesystem::path(kDirKeys).string(), error)) return false;
  return ensure_dir(data_dir_ + "/" + std::filesystem::path(kDirSecrets).string(), error);
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
  std::wstring wide;
  if (!to_wide(data_dir_ + "/" + rel_path, &wide)) {
    set_win_error(error, "path utf8->utf16 failed");
    return false;
  }

  // 同目录下的临时文件，保证改名是同一卷内的原子操作。
  const std::wstring tmp = wide + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_HIDDEN, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    set_win_error(error, "CreateFile failed");
    return false;
  }
  bool ok = true;
  DWORD written = 0;
  const DWORD total = static_cast<DWORD>(bytes.size());
  if (total > 0) {
    ok = WriteFile(h, bytes.data(), total, &written, nullptr) != 0 && written == total;
  }
  if (ok) ok = FlushFileBuffers(h) != 0;
  CloseHandle(h);
  if (!ok) {
    DeleteFileW(tmp.c_str());
    set_win_error(error, "write failed");
    return false;
  }
  if (!MoveFileExW(tmp.c_str(), wide.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp.c_str());
    set_win_error(error, "MoveFileEx failed");
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
  std::wstring wide;
  if (!to_wide(data_dir_ + "/" + rel_path, &wide)) {
    set_win_error(error, "path utf8->utf16 failed");
    return false;
  }
  HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    set_win_error(error, "CreateFile failed");
    return false;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 ||
      size.QuadPart > static_cast<LONGLONG>(0xFFFFFFFF)) {
    CloseHandle(h);
    set_win_error(error, "GetFileSizeEx failed or too large");
    return false;
  }
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(size.QuadPart));
  DWORD read_total = 0;
  bool ok = true;
  while (read_total < buf.size()) {
    DWORD chunk = 0;
    const DWORD want = static_cast<DWORD>(
        (buf.size() - read_total) > 0x10000000u ? 0x10000000u : (buf.size() - read_total));
    if (!ReadFile(h, buf.data() + read_total, want, &chunk, nullptr) || chunk == 0) {
      ok = false;
      break;
    }
    read_total += chunk;
  }
  CloseHandle(h);
  if (!ok) {
    set_win_error(error, "ReadFile failed");
    return false;
  }
  buf.resize(read_total);
  *out = std::move(buf);
  return true;
}

bool FileStore::remove(const std::string& rel_path, std::string* error) const {
  if (!is_valid_rel_path(rel_path)) {
    if (error != nullptr) *error = "invalid relative path";
    return false;
  }
  std::wstring wide;
  if (!to_wide(data_dir_ + "/" + rel_path, &wide)) {
    set_win_error(error, "path utf8->utf16 failed");
    return false;
  }
  if (!DeleteFileW(wide.c_str())) {
    const DWORD err = GetLastError();
    if (err != ERROR_FILE_NOT_FOUND) {
      set_win_error(error, "DeleteFile failed");
      return false;
    }
  }
  return true;
}

}  // namespace secretkeeper::store
