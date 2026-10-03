// SecretKeeper - Linux platform layer interface.
//
// Only the pieces that genuinely require an OS API live behind this header:
// the data directory, the clipboard, and the native file chooser. Everything
// else comes from the shared implementation in src/common.
//
// The interface is deliberately identical on Windows, Linux and macOS so that
// app.cpp is shared verbatim by the three desktop platforms. Any member that
// cannot be implemented identically across platforms must not be added here.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace secretkeeper::ui {

// Per-user application data directory. Created on first use.
std::filesystem::path data_directory();

// Clipboard. All functions take/return UTF-8. The requirement mandates a timed
// clear after a copy; the timer itself lives in app.cpp, these are primitives.
bool clipboard_set(const std::string& utf8);
bool clipboard_get(std::string* out);
bool clipboard_clear();

// Native file chooser. Returns nullopt when the user cancels.
std::optional<std::string> choose_open_file(const std::string& title,
                                            const std::string& pattern);
std::optional<std::string> choose_save_file(const std::string& title,
                                            const std::string& pattern,
                                            const std::string& suggested);

// Last OS-level failure as a human readable UTF-8 string; empty when none.
std::string last_native_message();

}  // namespace secretkeeper::ui
