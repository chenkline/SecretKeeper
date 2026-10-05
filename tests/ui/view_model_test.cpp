// SecretKeeper - UI view model self-check.
//
// Covers the presentation rules that are hard requirements but need no FLTK
// event loop: list projection, the yellow question mark, fixed error wording,
// backoff formatting and the character counter.
//
// Test output must stay pure ASCII.

#include <cstdio>
#include <string>
#include <vector>

#include "core/service.h"
#include "view_model.h"

namespace svc = secretkeeper::service;
namespace ui = secretkeeper::ui;

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

svc::SecretListItem make_item(const char* id, const char* title, const char* mk_id,
                              const char* mk_name, bool found) {
  svc::SecretListItem it;
  it.secret_id = id;
  it.title = title;
  it.master_key_id = mk_id;
  it.master_key_name = mk_name;
  it.master_key_found = found;
  return it;
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("UI view model self-check\n");

  // ---- master key projection ----
  std::vector<svc::MasterKeyListItem> keys(2);
  keys[0].master_key_id = "aaaa";
  keys[0].name = "work key";
  keys[0].is_default = true;
  keys[1].master_key_id = "bbbb";
  keys[1].name = "";
  keys[1].is_default = false;

  const auto key_rows = ui::project_master_keys(keys);
  check(key_rows.size() == 2, "two master key rows projected");
  check(key_rows[0].c0 == "aaaa" && key_rows[0].c1 == "work key", "named key projected");
  check(!key_rows[0].c2.empty(), "default flag shown");
  check(key_rows[1].c1.empty(), "empty name stays empty");
  check(key_rows[1].c2.empty(), "non-default flag empty");
  check(!key_rows[0].show_question_mark && !key_rows[1].show_question_mark,
        "master key list never shows a question mark");

  // ---- the yellow question mark: driven by found, never by the name ----
  const std::vector<svc::SecretListItem> items = {
      make_item("s1", "t1", "MK-7A2F", "work key", true),    // found + named
      make_item("s2", "t2", "MK-9C10", "", true),            // found + EMPTY name
      make_item("s3", "t3", "MK-4D88", "", false),           // missing + empty name
  };
  const auto rows = ui::project_secrets(items);
  check(rows.size() == 3, "three secret rows projected");

  check(!rows[0].show_question_mark, "found key: no question mark");
  check(rows[0].c1 == "MK-7A2F", "found key: ID column unchanged");

  // The decisive case: the key exists but its name is empty. That must look
  // different from a missing key.
  check(!rows[1].show_question_mark, "found but unnamed key: NO question mark");
  check(rows[1].c1 == "MK-9C10", "found but unnamed key: ID column unchanged");
  check(rows[1].c2.empty(), "found but unnamed key: name column stays empty");

  check(rows[2].show_question_mark, "missing key: question mark shown");
  check(rows[2].c1.find("MK-4D88") != std::string::npos, "missing key: ID still shown");
  check(rows[2].c1.size() > std::string("MK-4D88").size(), "missing key: ID has a suffix");
  check(rows[2].c2.empty(), "missing key: name column empty");

  // The two "empty name" cases must be distinguishable.
  check(rows[1].c1 != rows[2].c1, "the two empty-name cases differ in the ID column");
  check(rows[1].show_question_mark != rows[2].show_question_mark,
        "the two empty-name cases differ in the question mark");

  check(ui::master_key_id_cell("MK-1", true) == "MK-1", "id cell: found has no suffix");
  check(ui::master_key_id_cell("MK-1", false).size() > 4, "id cell: missing has a suffix");

  // ---- error wording comes straight from the service ----
  check(ui::error_text(svc::Error::kPasswordWrong) == svc::message(svc::Error::kPasswordWrong),
        "error text matches the service wording");
  check(ui::error_text(svc::Error::kNeedsPassword) == svc::message(svc::Error::kNeedsPassword),
        "needs-password text matches the service wording");
  check(ui::error_text(svc::Error::kOk).empty(), "kOk has no text");

  // Every error code must map to a non-empty string except kOk, otherwise the UI
  // would show a blank status line.
  int missing_text = 0;
  for (int i = 0; i <= static_cast<int>(svc::Error::kInternal); ++i) {
    const auto e = static_cast<svc::Error>(i);
    if (e == svc::Error::kOk) continue;
    if (svc::message(e).empty()) ++missing_text;
  }
  check(missing_text == 0, "every error code carries fixed wording");

  // ---- backoff formatting ----
  svc::BackoffStatus b;
  b.waiting = false;
  check(ui::backoff_text(b).empty(), "no text when not waiting");
  b.waiting = true;
  b.remaining_seconds = 8;
  const std::string bt = ui::backoff_text(b);
  check(bt.find("8") != std::string::npos, "backoff text carries the remaining seconds");

  // ---- character counter ----
  check(ui::char_counter_text("abc") == "3 / 150", "counter on ascii");
  std::string cjk;
  for (int i = 0; i < 150; ++i) cjk += "\xe4\xb8\xad";
  check(ui::char_counter_text(cjk) == "150 / 150", "counter counts code points, not bytes");
  check(cjk.size() == 450, "the 150-CJK sample really is 450 bytes");

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
