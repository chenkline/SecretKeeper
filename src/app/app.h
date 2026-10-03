// SecretKeeper - main window (FLTK).
//
// The UI is a thin shell over the four business layers in src/common. It holds
// no key material of its own: every password that arrives is converted to bytes,
// handed to the service layer, and the Fl_Input holding the text is cleared
// immediately afterwards.
//
// Two rules from docs/04-requirements are implemented here and must not be
// "simplified" later:
//
//   1. The yellow question mark is appended to the master key ID *column value*
//      and marks "master key not found locally". It is completely independent of
//      whether the master key name column is empty.
//   2. Every user-visible error string comes from core::message(). This file
//      never assembles or rewrites wording.

#pragma once


namespace secretkeeper::ui {

// Starts the application. Returns the process exit code.
int run(int argc, char** argv);

}  // namespace secretkeeper::ui
