// SecretKeeper - macOS platform layer (Cocoa).
//
// FLTK's macOS port is built on Cocoa, so the clipboard and the native file
// chooser come straight from FLTK. Only the data directory needs code of our
// own: macOS has no XDG, the conventional location is ~/Library/Application
// Support.

#include <FL/Fl.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Box.H>

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

bool clipboard_set(const std::string& utf8) {
  // FLTK 1.4 replaced clipboard_set/paste with copy/paste. Destination 1 is the
  // system clipboard; on X11 the default (0) would only touch the selection
  // buffer, which a Ctrl+V from another application does not read.
  Fl::copy(utf8.c_str(), static_cast<int>(utf8.size()), 1);
  return true;
}

bool clipboard_get(std::string* out) {
  // Fl::paste() delivers the text through the receiver widget's callback rather
  // than returning it, so a hidden 1x1 Fl_Box collects it for us.
  out->clear();
  Fl_Box receiver(0, 0, 1, 1);
  receiver.hide();
  Fl::paste(receiver, 1);
  Fl::flush();
  if (const char* pasted = receiver.label(); pasted != nullptr && pasted[0] != '\0') {
    out->assign(pasted);
  }
  return !out->empty();
}

bool clipboard_clear() {
  Fl::copy("", 0, 1);
  return true;
}


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
