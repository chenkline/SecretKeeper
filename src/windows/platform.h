// SecretKeeper - Windows platform layer interface.
//
// Only the pieces that genuinely require a Win32 API live behind this header:
// the data directory, the clipboard, and the native file chooser. Everything
// else in the application is portable C++ from src/common.
//
// The interface is deliberately identical on Linux and macOS so that app.cpp can
// be shared between the three desktop platforms. Any member that cannot be
// implemented identically across platforms must not be added here.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace secretkeeper::ui {

// %APPDATA%\SecretKeeper. Created on first use.
std::filesystem::path data_directory();

// Clipboard. All functions take/return UTF-8. The requirement mandates a timed
// clear after a copy; the timer itself lives in app.cpp, these are primitives.
bool clipboard_set(const std::string& utf8);
bool clipboard_get(std::string* out);
bool clipboard_clear();

// Native file chooser. pattern is a FLTK filter pattern, e.g. "*.smkexp".
// Returns nullopt when the user cancels.
std::optional<std::string> choose_open_file(const std::string& title,
                                            const std::string& pattern);
std::optional<std::string> choose_save_file(const std::string& title,
                                            const std::string& pattern,
                                            const std::string& suggested);

// Last Win32 error as a human readable UTF-8 string; empty when there was none.
std::string last_native_message();

}  // namespace secretkeeper::ui
