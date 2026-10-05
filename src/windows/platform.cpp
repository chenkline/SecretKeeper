// SecretKeeper - Windows platform layer (thin OS wrappers).
//
// Everything in src/common is portable C++ and must not touch Win32 directly.
// The three things that genuinely need the OS live here:
//
//   1. the data directory (%APPDATA%\\SecretKeeper)
//   2. the clipboard (read/write, plus "clear after N seconds")
//   3. the native file chooser for import/export
//
// Nothing here contains business logic or key material. Passwords never pass
// through this layer.

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>

#include "platform.h"

namespace secretkeeper::ui {

namespace fs = std::filesystem;

namespace {

// Last failure reported by the native chooser, surfaced by last_native_message().
std::string last_error_;

// GetClipboardData / SetClipboardData need a length; a NUL terminator alone is
// ambiguous, so every transfer carries an explicit byte count.
bool put_clipboard_text(const std::string& utf8) {
  const int units = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
  if (units <= 0) return false;

  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(units + 1) * sizeof(wchar_t));
  if (mem == nullptr) return false;

  wchar_t* buffer = static_cast<wchar_t*>(GlobalLock(mem));
  if (buffer == nullptr) {
    GlobalFree(mem);
    return false;
  }
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                      buffer, units);
  buffer[units] = L'\0';
  GlobalUnlock(mem);

  if (!OpenClipboard(nullptr)) {
    GlobalFree(mem);
    return false;
  }
  EmptyClipboard();
  const bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
  // On success the clipboard owns the block now; on failure we still own it.
  if (!ok) GlobalFree(mem);
  CloseClipboard();
  return ok;
}

bool get_clipboard_text(std::string* out) {
  if (!OpenClipboard(nullptr)) return false;
  bool ok = false;
  if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle != nullptr) {
      const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
      if (text != nullptr) {
        const int units = static_cast<int>(wcslen(text));
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, units, nullptr, 0, nullptr, nullptr);
        if (bytes >= 0) {
          out->assign(static_cast<std::size_t>(bytes), '\0');
          if (bytes == 0 ||
              WideCharToMultiByte(CP_UTF8, 0, text, units, out->data(), bytes, nullptr, nullptr) == bytes) {
            ok = true;
          }
        }
        GlobalUnlock(handle);
      }
    }
  }
  CloseClipboard();
  return ok;
}

}  // namespace

fs::path data_directory() {
  // 必须持有 wstring 本身而不是它的 data() 指针：buffer 是局部对象，
  // 函数返回前析构，留下的指针是悬垂的，后续用它构造 path 会读到
  // 已释放的堆内存，表现为目录名变成乱码。
  std::wstring roaming;
  const DWORD len = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
  if (len > 0) {
    roaming.resize(len);
    const DWORD got = GetEnvironmentVariableW(L"APPDATA", roaming.data(), len);
    if (got > 0) {
      roaming.resize(got);
    } else {
      roaming.clear();
    }
  }

  fs::path base = roaming.empty() ? fs::temp_directory_path() : fs::path(roaming);
  return base / L"SecretKeeper";
}

bool clipboard_set(const std::string& utf8) {
  return put_clipboard_text(utf8);
}

bool clipboard_get(std::string* out) {
  return get_clipboard_text(out);
}

bool clipboard_clear() {
  if (!OpenClipboard(nullptr)) return false;
  EmptyClipboard();
  CloseClipboard();
  return true;
}

std::optional<std::string> choose_open_file(const std::string& title,
                                            const std::string& pattern) {
  Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_FILE);
  chooser.title(title.c_str());
  // The filter string is "Label\tpattern" with one entry per line.
  chooser.filter(pattern.c_str());
  switch (chooser.show()) {
    case 1:
      return std::nullopt;  // user cancelled
    case -1:
      last_error_ = chooser.errmsg() != nullptr ? chooser.errmsg() : "文件对话框不可用";
      return std::nullopt;
    default:
      break;
  }
  const char* picked = chooser.filename();
  if (picked == nullptr) return std::nullopt;
  return std::string(picked);
}

std::optional<std::string> choose_save_file(const std::string& title,
                                            const std::string& pattern,
                                            const std::string& suggested) {
  Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
  chooser.title(title.c_str());
  chooser.filter(pattern.c_str());
  chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM |
                  Fl_Native_File_Chooser::USE_FILTER_EXT);
  chooser.preset_file(suggested.c_str());
  switch (chooser.show()) {
    case 1:
      return std::nullopt;  // user cancelled
    case -1:
      last_error_ = chooser.errmsg() != nullptr ? chooser.errmsg() : "文件对话框不可用";
      return std::nullopt;
    default:
      break;
  }
  const char* picked = chooser.filename();
  if (picked == nullptr) return std::nullopt;
  return std::string(picked);
}

std::string last_native_message() {
  if (!last_error_.empty()) return last_error_;
  const unsigned long code = GetLastError();
  if (code == 0) return {};
  LPWSTR text = nullptr;
  const DWORD n = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&text), 0, nullptr);
  std::string out;
  if (n != 0 && text != nullptr) {
    std::wstring wide(text, n);
    while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n')) {
      wide.pop_back();
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                          static_cast<int>(wide.size()), nullptr, 0,
                                          nullptr, nullptr);
    if (bytes > 0) {
      out.assign(static_cast<std::size_t>(bytes), '\0');
      WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                          out.data(), bytes, nullptr, nullptr);
    }
  }
  if (text != nullptr) LocalFree(text);
  return out;
}

}  // namespace secretkeeper::ui
