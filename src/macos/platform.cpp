// SecretKeeper - macOS platform layer (Cocoa).
//
// FLTK's macOS port is built on Cocoa, so the clipboard and the native file
// chooser come straight from FLTK. Only the data directory needs code of our
// own: macOS has no XDG, the conventional location is ~/Library/Application
// Support.

#include <FL/Fl.H>
#include <FL/Fl_Native_File_Chooser.H>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include "platform.h"

namespace secretkeeper::ui {

namespace fs = std::filesystem;

namespace {

constexpr int kChooserError = -1;
constexpr int kChooserCancel = 1;

std::string last_error_;

}  // namespace

fs::path data_directory() {
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return fs::path(home) / "Library" / "Application Support" / "SecretKeeper";
  }
  return fs::temp_directory_path() / "SecretKeeper";
}

bool clipboard_set(const std::string& utf8) { return Fl::clipboard_set(utf8.c_str()); }

bool clipboard_get(std::string* out) {
  if (Fl::clipboard_paste() == nullptr) return false;
  out->assign(Fl::clipboard_paste());
  return true;
}

bool clipboard_clear() { return Fl::clipboard_set(""); }

std::optional<std::string> choose_open_file(const std::string& title,
                                            const std::string& pattern) {
  Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_FILE);
  chooser.title(title.c_str());
  chooser.filter(pattern.c_str());
  const int rc = chooser.show();
  if (rc == kChooserCancel) return std::nullopt;
  if (rc == kChooserError) {
    last_error_ = chooser.errmsg() != nullptr ? chooser.errmsg() : "文件对话框不可用";
    return std::nullopt;
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
  const int rc = chooser.show();
  if (rc == kChooserCancel) return std::nullopt;
  if (rc == kChooserError) {
    last_error_ = chooser.errmsg() != nullptr ? chooser.errmsg() : "文件对话框不可用";
    return std::nullopt;
  }
  const char* picked = chooser.filename();
  if (picked == nullptr) return std::nullopt;
  return std::string(picked);
}

std::string last_native_message() { return last_error_; }

}  // namespace secretkeeper::ui
