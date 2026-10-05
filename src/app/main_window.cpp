// SecretKeeper - 主窗口实现
//
// 左侧主菜单三项，路由到九个页面（docs/ui/svg/ 的 01-09 号设计稿）。
// 定时器有三：KEK 轮换（需求 2.7 的 30 秒）、闲置锁定（4.1）、
// 剪贴板定时清除（4.3）。

#include "main_window.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "app.h"
#include "platform.h"
#include "view_model.h"

namespace secretkeeper::ui {
namespace {

namespace svc = secretkeeper::service;
using Clock = std::chrono::steady_clock;

constexpr int kWinW = 1180;
constexpr int kWinH = 800;

// KEK 轮换间隔固定 30 秒（需求 2.7），不随安全设置变化。
constexpr double kKekRotateSeconds = 30.0;

struct MenuDef {
  Page page;
  const char* label;
  widgets::Icon icon;
};

const MenuDef kMenu[3] = {
    {Page::kMasterKeys, "主密钥管理", widgets::Icon::kKey},
    {Page::kSecrets, "机密信息管理", widgets::Icon::kFile},
    {Page::kSettings, "安全设置", widgets::Icon::kSettings},
};

constexpr int kMenuTop = 140;
constexpr int kMenuItemH = 44;
constexpr int kMenuGap = 10;

}  // namespace

MainWindow::MainWindow() : Fl_Double_Window(kWinW, kWinH, "密匣 SecretKeeper") {
  color(theme::kContentBg);
  begin();

  // 每个页面都是 Fl_Group，构造时已 end() 封口（页面内的控件挂到页面自身）。
  // 构造完成后必须显式 add() 把页面挂到窗口的 children 上——FLTK 不会
  // 自动挂载用 new 创建的 Group。缺这一步，show()/hide() 会访问空 parent。
  unlock_ = std::make_unique<UnlockPage>(this);
  keys_ = std::make_unique<MasterKeysPage>(this);
  secrets_ = std::make_unique<SecretsPage>(this);
  settings_page_ = std::make_unique<SettingsPage>(this);
  create_key_ = std::make_unique<CreateKeyPage>(this);
  import_key_ = std::make_unique<ImportKeyPage>(this);
  export_key_ = std::make_unique<ExportKeyPage>(this);
  add_secret_ = std::make_unique<AddSecretPage>(this);
  import_secret_ = std::make_unique<ImportSecretPage>(this);
  for (Fl_Group* page : all_pages()) {
    if (page == nullptr) continue;
    add(page);
    page->hide();
  }

  build_menu();

  {
    lock_btn_ = new widgets::IconButton(kWinW - theme::kContentPad - 108, 8, 108, 32,
                                        widgets::Icon::kLock, "立即锁定",
                                        widgets::ButtonKind::kSecondary);
    lock_btn_->callback([](Fl_Widget*, void* data) {
      static_cast<MainWindow*>(data)->lock_now();
      static_cast<MainWindow*>(data)->navigate(Page::kMasterKeys);
    }, this);
  }

  {
    status_ = new Fl_Box(theme::kSidebarWidth + theme::kContentPad, kWinH - 40,
                         kWinW - theme::kSidebarWidth - theme::kContentPad * 2, 26, "");
    status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    status_->labelsize(theme::kFontSmall);
    status_->labelcolor(theme::kTextMuted);
  }

  end();

  // 打开数据目录并初始化 service。
  std::error_code ec;
  const std::filesystem::path dir = data_directory();
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    set_status("无法创建数据目录");
  } else if (service_.open(dir.string()) != svc::Error::kOk) {
    set_status("无法打开索引库");
  }

  navigate(Page::kMasterKeys);
  Fl::add_timeout(kKekRotateSeconds, on_rotate_tick, this);
  Fl::repeat_timeout(1.0, on_idle_tick, this);
}

MainWindow::~MainWindow() {
  Fl::remove_timeout(on_rotate_tick, this);
  Fl::remove_timeout(on_idle_tick, this);
  Fl::remove_timeout(on_clipboard_clear, this);
  service_.close();
}

std::array<Fl_Group*, 9> MainWindow::all_pages() const {
  return {unlock_.get(),  keys_.get(),         secrets_.get(),
          settings_page_.get(), create_key_.get(), import_key_.get(),
          export_key_.get(), add_secret_.get(), import_secret_.get()};
}

void MainWindow::build_menu() {
  for (int i = 0; i < 3; ++i) {
    menu_targets_[i].window = this;
    menu_targets_[i].page = kMenu[i].page;
    auto* btn = new widgets::Button(theme::kGapLg, kMenuTop + i * (kMenuItemH + kMenuGap),
                                    theme::kSidebarWidth - theme::kGapLg * 2, kMenuItemH,
                                    kMenu[i].label, widgets::ButtonKind::kSecondary);
    // FLTK 回调的第二个参数是 void*，这里放「窗口 + 页号」的组合。
    btn->callback(on_menu_cb, &menu_targets_[i]);
    menu_items_[i] = btn;
  }
}

void MainWindow::on_menu_cb(Fl_Widget*, void* data) {
  auto* target = static_cast<MenuTarget*>(data);
  target->window->navigate(target->page);
}

void MainWindow::draw() {
  Fl_Double_Window::draw();
  // 左侧栏底色（菜单项已在 build_menu 中作为子控件添加）。
  fl_color(theme::kSidebarBg);
  fl_rectf(0, 0, theme::kSidebarWidth, kWinH);

  // Logo 区
  widgets::draw_icon(widgets::Icon::kKey, theme::kGapXl + 8, 56, theme::kSidebarActiveText);
  fl_color(0xFFFFFF);
  fl_font(theme::font_for(theme::kWeightBold), 20);
  fl_draw("密匣", theme::kGapXl + 40, 52, 120, 28, FL_ALIGN_LEFT);
  fl_color(theme::kSidebarIcon);
  fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
  fl_draw("SecretKeeper", theme::kGapXl + 40, 80, 140, 18, FL_ALIGN_LEFT);
}

int MainWindow::handle(int event) {
  switch (event) {
    case FL_PUSH:
    case FL_KEYDOWN:
    case FL_MOUSEWHEEL:
      note_activity();
      break;
    case FL_UNFOCUS:
    case FL_HIDE:
      // 需求 4.4：离开窗口立即隐藏明文。
      if (settings_.hide_plaintext_on_blur) hide_plaintext();
      break;
    default:
      break;
  }
  return Fl_Double_Window::handle(event);
}

void MainWindow::hide_plaintext() { secrets_->hide_plaintext(); }

void MainWindow::navigate(Page page) {
  show_page(page);
  note_activity();
}

void MainWindow::go_back() {
  switch (current_) {
    case Page::kCreateKey:
    case Page::kImportKey:
    case Page::kExportKey:
      navigate(Page::kMasterKeys);
      break;
    case Page::kAddSecret:
    case Page::kImportSecret:
      navigate(Page::kSecrets);
      break;
    default:
      navigate(Page::kMasterKeys);
      break;
  }
}

void MainWindow::show_page(Page page) {
  current_ = page;
  // 全部页面都是本窗口的子控件，按需显示。
  for (Fl_Group* page : all_pages()) page->hide();

  Fl_Group* active = nullptr;
  Page reload_target = page;
  switch (page) {
    case Page::kMasterKeys: active = keys_.get(); break;
    case Page::kSecrets: active = secrets_.get(); break;
    case Page::kSettings: active = settings_page_.get(); break;
    case Page::kCreateKey: active = create_key_.get(); break;
    case Page::kImportKey: active = import_key_.get(); break;
    case Page::kExportKey: active = export_key_.get(); break;
    case Page::kAddSecret: active = add_secret_.get(); break;
    case Page::kImportSecret: active = import_secret_.get(); break;
  }
  if (active == nullptr) return;
  active->show();
  active->redraw();

  // 刷新数据：子页面切回时也要 reload，否则显示旧值。
  switch (reload_target) {
    case Page::kMasterKeys:
    keys_->reload();
      break;
    case Page::kSecrets:
      secrets_->reload();
      break;
    case Page::kSettings:
      settings_page_->reload();
      break;
    case Page::kCreateKey:
      create_key_->reload();
      break;
    case Page::kImportKey:
      import_key_->reload();
      break;
    case Page::kExportKey:
      export_key_->reload();
      break;
    case Page::kAddSecret:
      add_secret_->reload();
      break;
    case Page::kImportSecret:
      import_secret_->reload();
      break;
  }

  // 菜单高亮：子页面归属到对应的管理页。
  Page menu_page = page;
  switch (page) {
    case Page::kCreateKey:
    case Page::kImportKey:
    case Page::kExportKey:
      menu_page = Page::kMasterKeys;
      break;
    case Page::kAddSecret:
    case Page::kImportSecret:
      menu_page = Page::kSecrets;
      break;
    default:
      break;
  }
  for (int i = 0; i < 3; ++i) {
    menu_items_[i]->set_selected(kMenu[i].page == menu_page);
    menu_items_[i]->redraw();
  }
  if (lock_btn_ != nullptr) lock_btn_->show();
  redraw();
}

void MainWindow::set_status(const std::string& text) {
  status_text_ = text;
  if (status_ != nullptr) {
    status_->copy_label(text.c_str());
    status_->redraw();
  }
}

void MainWindow::show_error(svc::Error e) { set_status(error_text(e)); }

void MainWindow::refresh() {
  keys_->reload();
  secrets_->reload();
  settings_page_->reload();
}

void MainWindow::note_activity() {
  last_activity_ = Clock::now();
  service_.notify_activity();
}

void MainWindow::on_unlocked() {
  hide_plaintext();
  navigate(Page::kSecrets);
  set_status("已解锁");
}

void MainWindow::lock_now() {
  service_.lock();
  hide_plaintext();
  set_status("已锁定");
  redraw();
}

void MainWindow::arm_clipboard_clear() {
  if (clipboard_armed_) Fl::remove_timeout(on_clipboard_clear, this);
  clipboard_armed_ = true;
  Fl::add_timeout(static_cast<double>(settings_.clipboard_clear_seconds), on_clipboard_clear,
                  this);
}

void MainWindow::on_clipboard_clear(void* data) {
  auto* self = static_cast<MainWindow*>(data);
  clipboard_clear();
  self->clipboard_armed_ = false;
  self->set_status("剪贴板已清除");
}

void MainWindow::on_rotate_tick(void* data) {
  auto* self = static_cast<MainWindow*>(data);
  // 需求 2.7：每 30 秒轮换一次内存中的 KEK；锁定时为 no-op。
  self->service_.rotate_kek();
  Fl::repeat_timeout(kKekRotateSeconds, on_rotate_tick, data);
}

void MainWindow::on_idle_tick(void* data) {
  static_cast<MainWindow*>(data)->check_idle();
  Fl::repeat_timeout(1.0, on_idle_tick, data);
}

void MainWindow::check_idle() {
  if (!service_.is_unlocked()) return;
  const auto idle = std::chrono::duration<double>(Clock::now() - last_activity_).count();
  if (idle >= settings_.idle_lock_minutes * 60.0) {
    lock_now();
  }
}

}  // namespace secretkeeper::ui
