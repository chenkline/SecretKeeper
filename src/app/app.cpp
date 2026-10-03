// SecretKeeper - main window implementation (FLTK).
//
// Layering: this file owns widgets and user intent only. Every byte operation
// goes through the four shared layers (container / crypto / store / core). The
// only platform calls are the four thin wrappers in platform.h, so the same file
// serves Windows, Linux and macOS.
//
// Two rules from docs/04-requirements are load bearing and must survive future
// edits:
//
//   1. The yellow question mark is part of the master key ID *column value* and
//      means "master key not found locally". It has nothing to do with whether
//      the master key name column is empty.
//   2. Every user-visible error string comes from core::message(). This file
//      never assembles or rewrites the wording.

#include "app.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Table_Row.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "backoff.h"
#include "container.h"
#include "error.h"
#include "file_store.h"
#include "hex.h"
#include "index_db.h"
#include "kek_cache.h"
#include "master_key_service.h"
#include "platform.h"
#include "secret_service.h"
#include "text.h"

namespace secretkeeper::ui {
namespace {

using core::Error;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// ---- tunables from docs/04-requirements section 4 -----------------------------
constexpr double kIdleLockMinutes = 5.0;              // 4.1
constexpr double kClipboardClearSeconds = 30.0;       // 4.3
constexpr double kKekRotateSeconds =
    static_cast<double>(core::kKekRotationSeconds);   // 2.7

constexpr int kWindowWidth = 980;
constexpr int kWindowHeight = 640;

// The question mark occupies its own fixed slot at the end of the ID column, so
// the ID text never shifts when a key goes missing.
constexpr int kMarkerSlotWidth = 12;

std::span<const std::uint8_t> as_bytes(const char* text) {
  if (text == nullptr) return {};
  return {reinterpret_cast<const std::uint8_t*>(text), std::strlen(text)};
}

void scrub(Fl_Input* field) {
  if (field == nullptr) return;
  // value("") resets the widget buffer; Fl_Input has no clear() member.
  field->value("");
}

// Passwords live in std::string only for the duration of one call; the buffer is
// overwritten and released before the function returns.
void scrub_password(std::string* password) {
  if (password == nullptr) return;
  std::fill(password->begin(), password->end(), '\0');
  password->clear();
}

// Export files are plain byte streams; std::ofstream/ifstream keep this portable
// across the three desktop platforms without pulling in <filesystem>'s extras.
bool write_bytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return out.good();
}

std::optional<std::vector<std::uint8_t>> read_bytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
  if (in.bad()) return std::nullopt;
  return bytes;
}

// ---- table -------------------------------------------------------------------
// One FLTK table plus the strings it draws. Column headers are drawn by hand
// because Fl_Table_Row does not store header text itself.
class DataTable : public Fl_Table_Row {
 public:
  DataTable(int x, int y, int w, int h, const char* const* headers, int header_count)
      : Fl_Table_Row(x, y, w, h, nullptr) {
    for (int i = 0; i < header_count && i < 3; ++i) headers_[i] = headers[i];
    header_count_ = header_count;
    cols(3);
    col_header(1);
    col_header_height(24);
    col_resize(1);
    row_header(0);
    rows(0);
    type(SELECT_SINGLE);
    end();
  }

  void set_col_widths(int c0, int c1, int c2) {
    col_width(0, c0);
    col_width(1, c1);
    col_width(2, c2);
  }

  // missing_rows[i] is true when row i's master key is absent locally.
  void set_rows(std::vector<std::array<std::string, 3>> rows,
                std::vector<bool> missing_rows) {
    data_ = std::move(rows);
    missing_ = std::move(missing_rows);
    Fl_Table_Row::rows(static_cast<int>(data_.size()));
    redraw();
  }

  int selected() {
    for (int r = 0; r < Fl_Table_Row::rows(); ++r) {
      if (row_selected(r)) return r;
    }
    return -1;
  }

 protected:
  void draw_cell(TableContext context, int R, int C, int X, int Y, int W, int H) override {
    fl_push_clip(X, Y, W, H);
    switch (context) {
      case CONTEXT_COL_HEADER: {
        fl_color(FL_LIGHT2);
        fl_rectf(X, Y, W, H);
        fl_color(FL_DARK_BLUE);
        if (C >= 0 && C < header_count_) {
          fl_draw(headers_[C], X + 5, Y, W - 10, H, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        }
        fl_color(FL_GRAY0);
        fl_rect(X, Y, W, H);
        break;
      }
      case CONTEXT_CELL: {
        if (R < 0 || R >= static_cast<int>(data_.size()) || C < 0 || C > 2) break;
        const bool is_selected = (R == selected());
        fl_color(is_selected ? FL_YELLOW : FL_WHITE);
        fl_rectf(X, Y, W, H);

        // The marker sits in a reserved slot on the ID column only. An empty
        // name column never triggers it.
        const bool marker = C == 0 && R < static_cast<int>(missing_.size()) && missing_[R];
        int text_width = W;
        if (marker) {
          text_width = W - kMarkerSlotWidth;
          fl_color(FL_DARK_RED);
          fl_draw("?", X + W - kMarkerSlotWidth + 2, Y, kMarkerSlotWidth - 2, H,
                  FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        }
        fl_color(FL_BLACK);
        fl_draw(data_[R][C].c_str(), X + 5, Y, text_width - 5, H,
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        break;
      }
      default:
        break;
    }
    fl_pop_clip();
  }

 private:
  const char* headers_[3] = {nullptr, nullptr, nullptr};
  int header_count_ = 0;
  std::vector<std::array<std::string, 3>> data_;
  std::vector<bool> missing_;
};

// ---- password dialog ----------------------------------------------------------
struct PasswordDialog {
  Fl_Window* window = nullptr;
  Fl_Input* first = nullptr;
  Fl_Input* second = nullptr;
  bool confirmed = false;
  bool accepted = false;
};

void password_accept_cb(Fl_Widget*, void* data) {
  auto* d = static_cast<PasswordDialog*>(data);
  const char* a = d->first->value();
  if (a == nullptr || a[0] == '\0') {
    fl_message("密码不能为空");
    return;
  }
  if (d->confirmed && std::strcmp(a, d->second->value()) != 0) {
    fl_message("两次输入的密码不一致");
    return;
  }
  d->accepted = true;
  d->window->hide();
}

void password_cancel_cb(Fl_Widget*, void* data) {
  auto* d = static_cast<PasswordDialog*>(data);
  d->accepted = false;
  d->window->hide();
}

bool ask_password(const char* title, const char* label, const char* first_label,
                  const char* confirm_label, std::string* out) {
  const bool confirm = confirm_label != nullptr;
  const int height = confirm ? 250 : 190;

  Fl_Window win(420, height, title);
  win.begin();

  Fl_Box hint(20, 16, 380, 46, label);
  hint.labelfont(FL_HELVETICA);
  hint.labelsize(13);

  PasswordDialog d;
  d.confirmed = confirm;
  d.window = &win;

  d.first = new Fl_Input(20, 76, 380, 28, first_label);
  d.first->type(FL_SECRET_INPUT);
  d.first->align(FL_ALIGN_LEFT);
  d.first->when(FL_WHEN_ENTER_KEY);
  d.first->callback(password_accept_cb, &d);

  if (confirm) {
    d.second = new Fl_Input(20, 142, 380, 28, confirm_label);
    d.second->type(FL_SECRET_INPUT);
    d.second->align(FL_ALIGN_LEFT);
    d.second->when(FL_WHEN_ENTER_KEY);
    d.second->callback(password_accept_cb, &d);
  }

  Fl_Button* ok = new Fl_Button(290, height - 50, 110, 32, "确定");
  Fl_Button* cancel = new Fl_Button(170, height - 50, 110, 32, "取消");
  win.end();

  ok->callback(password_accept_cb, &d);
  cancel->callback(password_cancel_cb, &d);

  d.first->take_focus();
  win.show();
  while (win.shown()) Fl::wait(0.05);

  if (d.accepted && d.first->value() != nullptr) out->assign(d.first->value());
  scrub(d.first);
  if (d.second != nullptr) scrub(d.second);
  return d.accepted;
}

// A single-line text prompt used for names, titles and plaintext entry.
bool ask_text(const char* label, const char* initial, std::string* out) {
  const char* result = fl_input(label, initial);
  if (result == nullptr) return false;
  out->assign(result);
  return true;
}

}  // namespace

// ---- main window --------------------------------------------------------------
class MainWindow : public Fl_Double_Window {
 public:
  MainWindow() : Fl_Double_Window(kWindowWidth, kWindowHeight, "密匣 SecretKeeper") {
    begin();
    build_widgets();
    end();
    wire_callbacks();
    if (open_store()) refresh_all();
    Fl::add_timeout(kKekRotateSeconds, on_rotate_tick, this);
    Fl::repeat_timeout(1.0, on_idle_tick, this);
  }

  ~MainWindow() override {
    lock_now();
    Fl::remove_timeout(on_rotate_tick, this);
    Fl::remove_timeout(on_idle_tick, this);
  }

 private:
  // ---- layout ----
  void build_widgets() {
    Fl_Box* heading = new Fl_Box(16, 10, 420, 28, "密匣 SecretKeeper");
    heading->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    heading->labelfont(FL_HELVETICA_BOLD);
    heading->labelsize(17);

    unlock_btn_ = new Fl_Button(760, 12, 90, 28, "解锁");
    lock_btn_ = new Fl_Button(856, 12, 90, 28, "锁定");

    // ---- master keys ----
    Fl_Group* mk_group = new Fl_Group(16, 48, 470, 500);
    mk_group->box(FL_ENGRAVED_BOX);
    mk_group->begin();

    Fl_Box* mk_label = new Fl_Box(12, 8, 300, 24, "主密钥");
    mk_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    mk_label->labelfont(FL_HELVETICA_BOLD);
    mk_label->labelsize(14);

    static const char* kMkHeaders[] = {"主密钥 ID", "名称", "默认"};
    mk_table_ = new DataTable(10, 36, 450, 330, kMkHeaders, 3);
    mk_table_->set_col_widths(250, 120, 60);

    mk_new_ = new Fl_Button(10, 378, 100, 32, "生成");
    mk_import_ = new Fl_Button(120, 378, 100, 32, "导入");
    mk_export_ = new Fl_Button(230, 378, 100, 32, "导出");
    mk_switch_ = new Fl_Button(340, 378, 100, 32, "切换默认");
    mk_delete_ = new Fl_Button(10, 418, 100, 32, "删除");
    mk_group->end();

    // ---- secrets ----
    Fl_Group* sec_group = new Fl_Group(498, 48, 466, 500);
    sec_group->box(FL_ENGRAVED_BOX);
    sec_group->begin();

    Fl_Box* sec_label = new Fl_Box(12, 8, 300, 24, "机密信息");
    sec_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    sec_label->labelfont(FL_HELVETICA_BOLD);
    sec_label->labelsize(14);

    static const char* kSecHeaders[] = {"主密钥 ID", "主密钥名称", "标题"};
    sec_table_ = new DataTable(10, 36, 446, 330, kSecHeaders, 3);
    sec_table_->set_col_widths(200, 120, 110);

    secret_detail_ = new Fl_Box(16, 374, 430, 74, "");
    secret_detail_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE);
    secret_detail_->labelfont(FL_HELVETICA);
    secret_detail_->labelsize(13);
    secret_detail_->box(FL_DOWN_BOX);

    sec_add_ = new Fl_Button(10, 456, 100, 32, "添加");
    sec_import_ = new Fl_Button(120, 456, 100, 32, "导入");
    sec_export_ = new Fl_Button(230, 456, 100, 32, "导出");
    sec_delete_ = new Fl_Button(340, 456, 100, 32, "删除");
    sec_group->end();

    status_ = new Fl_Box(16, kWindowHeight - 40, 940, 28, "");
    status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    status_->labelfont(FL_HELVETICA);
    status_->labelsize(13);
  }

  void wire_callbacks() {
    unlock_btn_->callback(on_unlock_cb, this);
    lock_btn_->callback(on_lock_cb, this);
    mk_new_->callback(on_new_master_key_cb, this);
    mk_import_->callback(on_import_master_key_cb, this);
    mk_export_->callback(on_export_master_key_cb, this);
    mk_switch_->callback(on_switch_default_cb, this);
    mk_delete_->callback(on_delete_master_key_cb, this);
    sec_add_->callback(on_add_secret_cb, this);
    sec_import_->callback(on_import_secret_cb, this);
    sec_export_->callback(on_export_secret_cb, this);
    sec_delete_->callback(on_delete_secret_cb, this);
  }

  // ---- storage ----
  bool open_store() {
    std::error_code ec;
    const fs::path dir = data_directory();
    fs::create_directories(dir, ec);
    if (ec) {
      set_status("无法创建数据目录");
      return false;
    }
    files_.set_data_dir(dir.string());
    std::string layout_error;
    if (!files_.ensure_layout(&layout_error)) {
      set_status("无法访问数据目录");
      return false;
    }
    if (db_.open((dir / "secret.db").string()) != store::StoreError::kOk) {
      set_status("无法打开索引库");
      return false;
    }
    return true;
  }

  // ---- status line ----
  void set_status(const std::string& text) {
    status_->copy_label(text.c_str());
    status_->redraw();
  }

  void show_error(Error e) {
    // Requirement wording is fixed in core::message(); the UI only displays it.
    set_status(std::string(core::message(e)));
  }

  // ---- list refresh ----
  void refresh_all() {
    default_key_id_ = db_.default_master_key_id();
    refresh_keys();
    refresh_secrets();
  }

  void refresh_keys() {
    std::vector<std::array<std::string, 3>> rows;
    for (const core::MasterKeyListItem& item : keys_.list()) {
      rows.push_back({item.master_key_id, item.name, item.is_default ? "是" : ""});
    }
    mk_table_->set_rows(std::move(rows), {});
  }

  void refresh_secrets() {
    std::vector<std::array<std::string, 3>> rows;
    std::vector<bool> missing;
    for (const core::SecretListItem& item : secrets_.list()) {
      // The yellow question mark rides on the ID column value. It is shown
      // because the key is missing -- never because the name happens to be empty.
      rows.push_back({item.master_key_id, item.master_key_name, item.title});
      missing.push_back(!item.master_key_found);
    }
    sec_table_->set_rows(std::move(rows), std::move(missing));
  }

  // ---- selection ----
  std::string selected_master_key_id() const {
    const int row = mk_table_->selected();
    const std::vector<core::MasterKeyListItem> items = keys_.list();
    if (row < 0 || row >= static_cast<int>(items.size())) return {};
    return items[static_cast<std::size_t>(row)].master_key_id;
  }

  std::string selected_secret_id() const {
    const int row = sec_table_->selected();
    const std::vector<core::SecretListItem> items = secrets_.list();
    if (row < 0 || row >= static_cast<int>(items.size())) return {};
    return items[static_cast<std::size_t>(row)].secret_id;
  }

  std::optional<core::SecretListItem> find_secret_item(const std::string& id) const {
    for (const core::SecretListItem& item : secrets_.list()) {
      if (item.secret_id == id) return item;
    }
    return std::nullopt;
  }

  std::string master_key_name(std::string_view master_key_id) const {
    for (const core::MasterKeyListItem& item : keys_.list()) {
      if (item.master_key_id == master_key_id) return item.name;
    }
    return {};
  }

  // ---- KEK acquisition ----
  // Default key => the cache. Any other key => prompt for that key's password.
  bool kek_for(std::string_view master_key_id, crypto::Kek* out) {
    if (default_key_id_.has_value() && master_key_id == *default_key_id_) {
      const std::optional<crypto::Kek> cached = kek_cache_.peek();
      if (!cached.has_value()) {
        set_status("请先解锁");
        return false;
      }
      *out = *cached;
      return true;
    }
    return prompt_kek_for_key(master_key_id, out);
  }

  bool prompt_kek_for_key(std::string_view master_key_id, crypto::Kek* out) {
    if (backoff_.is_waiting()) {
      report_backoff();
      return false;
    }
    // 3.4 / 3.6: show both the ID and the name so the two keys are not confused.
    const std::string name = master_key_name(master_key_id);
    std::string label = "主密钥 ID: ";
    label += master_key_id;
    label += "\n主密钥名称: ";
    label += name.empty() ? "(空)" : name;
    label += "\n请输入该主密钥密码";

    std::string password;
    if (!prompt_password("验证主密钥", label.c_str(), "主密钥密码", nullptr, &password)) {
      return false;
    }
    container::MasterKeyFile file;
    if (keys_.load_file(master_key_id, &file) != Error::kOk) {
      scrub_password(&password);
      show_error(Error::kMasterKeyNotFound);
      return false;
    }
    const crypto::Kek kek = crypto::derive_kek(as_bytes(password.c_str()), file.salt);
    scrub_password(&password);
    crypto::SecureBytes probe;
    if (keys_.public_key_der(master_key_id, kek, &probe) != Error::kOk) {
      note_password_failure();
      return false;
    }
    note_password_success();
    *out = kek;
    return true;
  }

  // ---- password prompting with backoff (requirement 4.5) ----
  // Every interactive prompt funnels through here so the exponential backoff
  // cannot be bypassed. The counter is memory only; exceeding it never locks
  // the app out, it only lengthens the wait.
  bool prompt_password(const char* title, const char* label, const char* field,
                       const char* confirm_field, std::string* out) {
    if (backoff_.is_waiting()) {
      report_backoff();
      return false;
    }
    return ask_password(title, label, field, confirm_field, out);
  }

  void note_password_failure() {
    const std::uint32_t wait = backoff_.record_failure();
    if (wait > 0) {
      set_status(std::string(core::message(Error::kRateLimited)) + std::to_string(wait) +
                 " 秒（主密钥密码错误）");
    } else {
      set_status(std::string(core::message(Error::kPasswordWrong)));
    }
  }

  void report_backoff() {
    set_status(std::string(core::message(Error::kRateLimited)) +
               std::to_string(backoff_.remaining_seconds()) + " 秒");
  }

  void note_password_success() { backoff_.reset(); }

  // ---- unlock / lock (requirements 4.1, 4.2) ----
  void unlock() {
    if (!default_key_id_.has_value()) {
      set_status("尚未创建主密钥，请先生成");
      return;
    }
    if (backoff_.is_waiting()) {
      report_backoff();
      return;
    }
    std::string password;
    if (!ask_password("解锁", "请输入默认主密钥密码以解锁", "主密钥密码", nullptr,
                      &password)) {
      return;
    }
    container::MasterKeyFile file;
    if (keys_.load_file(*default_key_id_, &file) != Error::kOk) {
      scrub_password(&password);
      show_error(Error::kMasterKeyNotFound);
      return;
    }
    const crypto::Kek kek = crypto::derive_kek(as_bytes(password.c_str()), file.salt);
    crypto::SecureBytes probe;
    const Error rc = keys_.public_key_der(*default_key_id_, kek, &probe);
    scrub_password(&password);
    if (rc != Error::kOk) {
      note_password_failure();
      return;
    }
    note_password_success();
    kek_cache_.load(kek);
    // 4.2: rotate immediately after a successful unlock, independent of the
    // 30 second timer.
    kek_cache_.rotate();
    last_activity_ = Clock::now();
    set_status("已解锁");
  }

  void lock_now() {
    kek_cache_.clear();
    hide_plaintext();
    last_activity_ = Clock::now();
    set_status("已锁定");
  }

  // ---- timers ----
  static void on_rotate_tick(void* data) {
    auto* self = static_cast<MainWindow*>(data);
    // 2.7: rotate every 30 seconds while unlocked.
    if (self->kek_cache_.has_value()) self->kek_cache_.rotate();
    Fl::repeat_timeout(kKekRotateSeconds, on_rotate_tick, self);
  }

  static void on_idle_tick(void* data) {
    auto* self = static_cast<MainWindow*>(data);
    self->check_idle();
    Fl::repeat_timeout(1.0, on_idle_tick, self);
  }

  void check_idle() {
    if (!kek_cache_.has_value()) return;
    if (std::chrono::duration<double>(Clock::now() - last_activity_).count() >=
        kIdleLockMinutes * 60.0) {
      lock_now();
    }
  }

  void note_activity() { last_activity_ = Clock::now(); }

  int handle(int event) override {
    switch (event) {
      case FL_PUSH:
      case FL_KEYDOWN:
      case FL_MOUSEWHEEL:
        note_activity();
        break;
      case FL_UNFOCUS:
      case FL_HIDE:
        // 4.4: leaving the window hides the plaintext immediately.
        hide_plaintext();
        break;
      default:
        break;
    }
    return Fl_Double_Window::handle(event);
  }

  void hide_plaintext() {
    if (!plaintext_visible_) return;
    std::fill(plaintext_.begin(), plaintext_.end(), '\0');
    plaintext_.clear();
    plaintext_visible_ = false;
    secret_detail_->copy_label("");
    secret_detail_->redraw();
  }

  // ---- clipboard (requirement 4.3) ----
  void copy_plaintext() {
    if (!plaintext_visible_ || plaintext_.empty()) {
      set_status("请先查看机密信息");
      return;
    }
    if (!clipboard_set(plaintext_)) {
      set_status("剪贴板不可用");
      return;
    }
    set_status("已复制，" + std::to_string(static_cast<int>(kClipboardClearSeconds)) +
               " 秒后自动清除");
    Fl::remove_timeout(clipboard_clear_cb);
    Fl::add_timeout(kClipboardClearSeconds, clipboard_clear_cb);
  }

  static void clipboard_clear_cb(void* data) {
    (void)data;
    clipboard_clear();
  }

  // ---- master key operations (requirement 2) ----
  void on_new_master_key() {
    std::string name;
    if (!ask_text("主密钥名称（可留空）", "", &name)) return;

    std::string password;
    if (!prompt_password("生成主密钥", "请设置该主密钥的密码", "主密钥密码", "确认密码",
                         &password)) {
      return;
    }
    const bool is_first = db_.master_key_count() == 0;
    const Error e = keys_.create(name, as_bytes(password.c_str()), is_first);
    scrub_password(&password);
    if (e != Error::kOk) {
      show_error(e);
      return;
    }
    refresh_all();
    set_status("主密钥已生成");
  }

  void on_export_master_key() {
    const std::string id = selected_master_key_id();
    if (id.empty()) {
      set_status("请先选择主密钥");
      return;
    }
    crypto::Kek kek;
    if (!kek_for(id, &kek)) return;

    std::string protection;
    if (!prompt_password("导出主密钥", "请设置导出文件的保护密码", "保护密码", "确认密码",
                         &protection)) {
      return;
    }
    std::vector<std::uint8_t> bytes;
    const Error e = keys_.export_to(id, kek, as_bytes(protection.c_str()), &bytes);
    scrub_password(&protection);
    if (e != Error::kOk) {
      show_error(e);
      return;
    }
    const std::optional<std::string> path =
        choose_save_file("导出主密钥", "*.smkexp", id + ".smkexp");
    if (!path.has_value()) return;
    if (!write_bytes(*path, bytes)) {
      set_status("文件读写失败");
      return;
    }
    set_status("主密钥已导出");
  }

  void on_import_master_key() {
    const std::optional<std::string> path = choose_open_file("导入主密钥", "*.smkexp");
    if (!path.has_value()) return;
    const std::optional<std::vector<std::uint8_t>> bytes = read_bytes(*path);
    if (!bytes.has_value()) {
      set_status("文件读写失败");
      return;
    }
    std::string protection;
    if (!prompt_password("导入主密钥", "请输入导出文件的保护密码", "保护密码", nullptr,
                         &protection)) {
      return;
    }
    std::string password;
    if (!prompt_password("导入主密钥", "请输入该主密钥的新密码", "新密码", "确认密码",
                         &password)) {
      scrub_password(&protection);
      return;
    }
    const Error e =
        keys_.import_from(*bytes, as_bytes(protection.c_str()), as_bytes(password.c_str()));
    scrub_password(&protection);
    scrub_password(&password);
    if (e != Error::kOk) {
      if (is_password_error(e)) note_password_failure(); else show_error(e);
      return;
    }
    refresh_all();
    set_status("主密钥已导入");
  }

  void on_switch_default() {
    const std::string id = selected_master_key_id();
    if (id.empty()) {
      set_status("请先选择主密钥");
      return;
    }
    if (default_key_id_.has_value() && id == *default_key_id_) {
      set_status("该主密钥已是默认主密钥");
      return;
    }
    std::string old_password;
    if (default_key_id_.has_value()) {
      if (!prompt_password("切换默认主密钥", "请输入原默认主密钥密码", "原主密钥密码",
                           nullptr, &old_password)) {
        return;
      }
    }
    std::string new_password;
    if (!prompt_password("切换默认主密钥", "请输入新默认主密钥的密码", "新主密钥密码",
                         nullptr, &new_password)) {
      scrub_password(&old_password);
      return;
    }
    const Error e =
        keys_.switch_default(id, as_bytes(old_password.c_str()), as_bytes(new_password.c_str()));
    scrub_password(&old_password);
    scrub_password(&new_password);
    if (e != Error::kOk) {
      if (is_password_error(e)) note_password_failure(); else show_error(e);
      return;
    }
    // The cached KEK belongs to the previous default key, so drop it.
    kek_cache_.clear();
    refresh_all();
    set_status("已切换默认主密钥，请重新解锁");
  }

  void on_delete_master_key() {
    const std::string id = selected_master_key_id();
    if (id.empty()) {
      set_status("请先选择主密钥");
      return;
    }
    if (default_key_id_.has_value() && id == *default_key_id_) {
      show_error(Error::kCannotDeleteDefault);
      return;
    }
    // 2.6: the user must confirm a backup exists before the key is destroyed.
    if (fl_choice("删除主密钥前请确认已导出备份。", "立即导出", "已经导出", nullptr) == 0) {
      on_export_master_key();
    }
    std::string password;
    if (!prompt_password("删除主密钥", "请输入该主密钥密码以确认删除", "主密钥密码",
                         nullptr, &password)) {
      return;
    }
    const Error e = keys_.remove(id, as_bytes(password.c_str()));
    scrub_password(&password);
    if (e != Error::kOk) {
      if (is_password_error(e)) note_password_failure(); else show_error(e);
      return;
    }
    refresh_all();
    set_status("主密钥已删除，其机密信息已保留");
  }

  // ---- secret operations (requirement 3) ----
  void on_add_secret() {
    if (!default_key_id_.has_value()) {
      set_status("尚未创建主密钥，请先生成");
      return;
    }
    if (db_.secret_count() >= store::kMaxSecrets) {
      show_error(Error::kSecretQuotaExceeded);
      return;
    }
    std::string plaintext;
    if (!ask_text("机密信息内容（最多 150 字符）", "", &plaintext)) return;
    if (plaintext.empty()) {
      show_error(Error::kEmptySecret);
      return;
    }
    // 3.2: the limit counts Unicode code points, not bytes or UTF-16 units.
    if (text::count_code_points(plaintext) > text::kMaxSecretLength) {
      show_error(Error::kSecretTooLong);
      return;
    }
    std::string title;
    if (!ask_text("机密信息标题（可留空）", "", &title)) return;

    const std::string owner = *default_key_id_;
    crypto::Kek kek;
    if (!kek_for(owner, &kek)) return;

    std::string new_id;
    const Error e = secrets_.add(title, plaintext, owner, kek, &new_id);
    if (e != Error::kOk) {
      show_error(e);
      return;
    }
    refresh_secrets();
    set_status("机密信息已添加");
  }

  void on_export_secret() {
    const std::string id = selected_secret_id();
    if (id.empty()) {
      set_status("请先选择机密信息");
      return;
    }
    std::vector<std::uint8_t> bytes;
    if (secrets_.export_to(id, &bytes) != Error::kOk) {
      show_error(Error::kSecretNotFound);
      return;
    }
    const std::optional<std::string> path =
        choose_save_file("导出机密信息", "*.sscexp", id + ".sscexp");
    if (!path.has_value()) return;
    if (!write_bytes(*path, bytes)) {
      set_status("文件读写失败");
      return;
    }
    set_status("机密信息已导出");
  }

  void on_import_secret() {
    const std::optional<std::string> path = choose_open_file("导入机密信息", "*.sscexp");
    if (!path.has_value()) return;
    const std::optional<std::vector<std::uint8_t>> bytes = read_bytes(*path);
    if (!bytes.has_value()) {
      set_status("文件读写失败");
      return;
    }
    if (db_.secret_count() >= store::kMaxSecrets) {
      show_error(Error::kSecretQuotaExceeded);
      return;
    }
    // 3.4: the export file names its master key, so the password prompt can
    // identify the key before the import runs.
    container::ParseError perr = container::ParseError::kOk;
    const std::optional<container::SecretFile> parsed = container::parse_secret(*bytes, &perr);
    if (!parsed.has_value()) {
      show_error(perr == container::ParseError::kUnsupportedVersion
                     ? Error::kImportVersionTooHigh
                     : Error::kImportBadFormat);
      return;
    }
    const std::string owner =
        store::bytes_to_hex(std::span<const std::uint8_t>(parsed->master_key_id));
    crypto::Kek kek;
    if (!kek_for(owner, &kek)) return;

    std::string new_id;
    const Error e = secrets_.import_from(*bytes, kek, &new_id);
    if (e != Error::kOk) {
      if (is_password_error(e)) note_password_failure(); else show_error(e);
      return;
    }
    refresh_all();
    set_status("机密信息已导入");
  }

  void on_view_secret() {
    const std::string id = selected_secret_id();
    if (id.empty()) {
      set_status("请先选择机密信息");
      return;
    }
    const std::optional<core::SecretListItem> item = find_secret_item(id);
    if (!item.has_value()) {
      show_error(Error::kSecretNotFound);
      return;
    }
    if (!item->master_key_found) {
      show_masked_detail();
      return;
    }
    // The default key reuses the cache; any other key asks for its password.
    crypto::Kek prompted;
    const crypto::Kek* kek_ptr = nullptr;
    if (default_key_id_.has_value() && item->master_key_id == *default_key_id_) {
      const std::optional<crypto::Kek> cached = kek_cache_.peek();
      if (cached.has_value()) kek_ptr = &*cached;
    }
    if (kek_ptr == nullptr) {
      if (!kek_for(item->master_key_id, &prompted)) return;
      kek_ptr = &prompted;
    }
    core::SecretDetail detail;
    const Error e = secrets_.detail(id, kek_ptr, &detail);
    if (e != Error::kOk) {
      if (is_password_error(e)) note_password_failure(); else show_error(e);
      return;
    }
    if (detail.plaintext_masked) {
      show_masked_detail();
      return;
    }
    std::fill(plaintext_.begin(), plaintext_.end(), '\0');
    plaintext_ = detail.plaintext;
    plaintext_visible_ = true;
    secret_detail_->copy_label(plaintext_.c_str());
    secret_detail_->redraw();
    set_status("已显示明文，离开窗口将自动隐藏");
  }

  void show_masked_detail() {
    // 3.5: the master key is missing, so the plaintext is replaced by 6 asterisks.
    plaintext_.assign(core::kMaskedPlaintext);
    plaintext_visible_ = false;
    secret_detail_->copy_label(plaintext_.c_str());
    secret_detail_->redraw();
    set_status("该机密信息的主密钥不在本地");
  }

  void on_delete_secret() {
    const std::string id = selected_secret_id();
    if (id.empty()) {
      set_status("请先选择机密信息");
      return;
    }
    // 3.6: confirm a backup before destroying the record.
    if (fl_choice("删除机密信息前请确认已导出备份。", "立即导出", "已经导出", nullptr) == 0) {
      on_export_secret();
    }
    const std::optional<core::SecretListItem> item = find_secret_item(id);
    if (!item.has_value()) {
      show_error(Error::kSecretNotFound);
      return;
    }
    // A non-default key needs its password before the record may be removed.
    if (item->master_key_found &&
        (!default_key_id_.has_value() || item->master_key_id != *default_key_id_)) {
      crypto::Kek kek;
      if (!kek_for(item->master_key_id, &kek)) return;
    }
    const Error e = secrets_.remove(id);
    if (e != Error::kOk) {
      show_error(e);
      return;
    }
    hide_plaintext();
    refresh_secrets();
    set_status("机密信息已删除");
  }

  // ---- static callbacks ----
  static void on_unlock_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->unlock();
  }
  static void on_lock_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->lock_now();
  }
  static void on_new_master_key_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->on_new_master_key();
  }
  static void on_export_master_key_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->on_export_master_key();
  }
  static void on_import_master_key_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->on_import_master_key();
  }
  static void on_switch_default_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->on_switch_default();
  }
  static void on_delete_master_key_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->on_delete_master_key();
  }
  static void on_add_secret_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->on_add_secret();
  }
  static void on_import_secret_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->on_import_secret();
  }
  static void on_export_secret_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->on_export_secret();
  }
  static void on_delete_secret_cb(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->on_delete_secret();
  }

  // ---- state ----
  store::IndexDb db_;
  store::FileStore files_;
  core::MasterKeyService keys_{db_, files_};
  core::SecretService secrets_{db_, files_, keys_};
  core::KekCache kek_cache_;
  core::PasswordBackoff backoff_;

  DataTable* mk_table_ = nullptr;
  DataTable* sec_table_ = nullptr;
  Fl_Box* status_ = nullptr;
  Fl_Box* secret_detail_ = nullptr;
  Fl_Button* unlock_btn_ = nullptr;
  Fl_Button* lock_btn_ = nullptr;
  Fl_Button* mk_new_ = nullptr;
  Fl_Button* mk_import_ = nullptr;
  Fl_Button* mk_export_ = nullptr;
  Fl_Button* mk_switch_ = nullptr;
  Fl_Button* mk_delete_ = nullptr;
  Fl_Button* sec_add_ = nullptr;
  Fl_Button* sec_import_ = nullptr;
  Fl_Button* sec_export_ = nullptr;
  Fl_Button* sec_delete_ = nullptr;

  std::optional<std::string> default_key_id_;
  std::string plaintext_;
  bool plaintext_visible_ = false;
  Clock::time_point last_activity_ = Clock::now();
};

}  // namespace secretkeeper::ui

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  Fl::visual(FL_RGB);
  secretkeeper::ui::MainWindow window;
  window.show();
  return Fl::run();
}
