// SecretKeeper - 主窗口实现
//
// 左侧导航五项（02-09 号画板公共），路由到九个页面。
// 定时器有三：KEK 轮换（需求 2.7 的 30 秒）、闲置锁定（4.1）、
// 剪贴板定时清除（4.3）。
//
// 绘制顺序说明：窗口子控件按加入顺序绘制——九个页面在前，导航按钮等
// chrome 在后，因此页面自己的 draw() 先画好侧栏装饰（paint_sidebar_decor），
// chrome 控件再叠在其上。解锁页没有侧栏，chrome 在 show_page 里整体隐藏。

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

// KEK 轮换间隔固定 30 秒（需求 2.7），不随安全设置变化。
constexpr double kKekRotateSeconds = 30.0;

// 侧栏导航项（02 号画板最终稿）：三项，x=20 w=168 h=44，
// 客户区 y=99 起，间距 52。文件导入/导出入口在各管理页内（用户定稿）。
constexpr int kNavX = 20;
constexpr int kNavW = 168;
constexpr int kNavH = 44;
constexpr int kNavTop = 99;
constexpr int kNavStep = 52;

// 侧栏底部：锁定按钮距底 60（01/02/03 号画板一致）。
constexpr int kLockFromBottom = 60;
constexpr int kStatusFromBottom = 33;

// 容量卡（02 号画板最终稿）：x=20 y=279 w=168 h=108，仅机密信息一根进度条。
constexpr int kQuotaCardX = 20;
constexpr int kQuotaCardY = 279;
constexpr int kQuotaCardW = 168;
constexpr int kQuotaCardH = 108;

// 内容页页脚：距底 53（02 号画板 786 = 839 − 53）。
constexpr int kFooterFromBottom = 53;

// 各页客户区高度 = 对应画板高度 − 36（自绘标题栏改用系统标题栏）。
int page_height(Page page) {
  switch (page) {
    case Page::kUnlock: return theme::kUnlockH;
    case Page::kMasterKeys: return theme::kKeysH;
    case Page::kSecrets: return theme::kSecretsH;
    case Page::kSettings: return theme::kSettingsH;
    case Page::kCreateKey: return theme::kCreateKeyH;
    case Page::kImportKey: return theme::kImportKeyH;
    case Page::kExportKey: return theme::kExportKeyH;
    case Page::kAddSecret: return theme::kAddSecretH;
    case Page::kImportSecret: return theme::kImportSecretH;
  }
  return theme::kKeysH;
}

}  // namespace

MainWindow::MainWindow()
    : Fl_Double_Window(theme::kWinW, theme::kUnlockH, "密匣 SecretKeeper") {
  color(theme::kContentBg);
  begin();

  // 每个页面都是 Fl_Group，构造时已 end() 封口（页面内的控件挂到页面自身）。
  // **不能再 add()**：Fl_Widget 的构造器末尾会把自己挂到
  // Fl_Group::current()（此时就是本窗口），Fl_Group::add() 又是
  // 无去重的 insert(o, children_)，重复调用会让同一节点在 children
  // 链上出现两次，之后 hide()/resize() 遍历时必然出错。
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
    page->hide();
  }

  build_chrome();

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

  // 必须在 Fl::run() 之前显示窗口：没有任何可见窗口时事件循环会立即
  // 返回，进程表现为「一闪而过、无报错」。
  show();

  // 路由入口（01 号画板）：已有主密钥先解锁；首台设备没有主密钥，
  // 直接进主密钥管理页生成第一把。
  if (service_.list_master_keys().empty()) {
    navigate(Page::kMasterKeys);
  } else {
    navigate(Page::kUnlock);
  }

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

void MainWindow::build_chrome() {
  // 导航三项（02 号画板最终稿，顺序为用户定稿）：
  // 主密钥管理 -> 机密信息管理 -> 安全设置。
  struct NavDef {
    Page page;
    const char* label;
    widgets::Icon icon;
  };
  static const NavDef kNav[kNavCount] = {
      {Page::kMasterKeys, "主密钥管理", widgets::Icon::kKey},
      {Page::kSecrets, "机密信息管理", widgets::Icon::kFile},
      {Page::kSettings, "安全设置", widgets::Icon::kSettings},
  };
  for (int i = 0; i < kNavCount; ++i) {
    menu_targets_[i].window = this;
    menu_targets_[i].page = kNav[i].page;
    auto* btn = new widgets::IconButton(kNavX, kNavTop + i * kNavStep, kNavW, kNavH,
                                        kNav[i].icon, kNav[i].label,
                                        widgets::ButtonKind::kNav);
    btn->callback(on_menu_cb, &menu_targets_[i]);
    menu_items_[i] = btn;
  }

  // 容量卡文本（边框与进度条在 paint_sidebar_decor 里画）。
  quota_version_ = new Fl_Box(kQuotaCardX + 12, kQuotaCardY + 12, 144, 14,
                              "基础版 · 本地存储");
  quota_version_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  quota_version_->labelsize(theme::kFontTiny);
  quota_version_->labelcolor(theme::kSidebarIcon);

  quota_secrets_ = new Fl_Box(kQuotaCardX + 12, kQuotaCardY + 37, 144, 16, "机密信息 0 / 15");
  quota_secrets_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  quota_secrets_->labelsize(theme::kFontSmall);
  quota_secrets_->labelcolor(0xFFFFFF00);

  quota_keys_ = new Fl_Box(kQuotaCardX + 12, kQuotaCardY + 80, 144, 16, "主密钥 0 / 2");
  quota_keys_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  quota_keys_->labelsize(theme::kFontSmall);
  quota_keys_->labelcolor(0xFFFFFF00);

  // 锁定按钮（02 号画板底部，kGhost 样式）。位置随页高变化，在 show_page 摆放。
  lock_btn_ = new widgets::IconButton(kNavX, 0, kNavW, 40, widgets::Icon::kLock,
                                      "立即锁定", widgets::ButtonKind::kGhost);
  lock_btn_->labelsize(theme::kFontSmall);
  lock_btn_->callback([](Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->lock_now();
  }, this);

  // 瞬时状态消息（最终稿侧栏底部没有常驻离线行）：默认隐藏。
  status_ = new Fl_Box(kNavX, 0, kNavW, 14, "");
  status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  status_->labelsize(theme::kFontTiny);
  status_->labelcolor(theme::kTextFaint);
  status_->hide();

  // 内容页页脚的安全状态胶囊（文案由 show_page 后的 reload 刷新）。
  state_pill_ = new widgets::Pill(theme::kWinW - theme::kContentPad - 49, 0, 49, 21,
                                  "已锁定", theme::kAccent, theme::kAccentSoft);
  state_pill_->set_shown(false);

  // 页脚安全状态行（02 号画板）：盾标 + 固定文案，右侧是上面的胶囊。
  footer_text_ = new Fl_Box(theme::kSidebarWidth + theme::kContentPad, 0, 700, 21,
                            "本机加密保存 · 无网络连接 · 无云同步");
  footer_text_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  footer_text_->labelsize(theme::kFontTiny);
  footer_text_->labelcolor(theme::kTextMuted);
  footer_text_->hide();
}

void MainWindow::on_menu_cb(Fl_Widget*, void* data) {
  auto* target = static_cast<MenuTarget*>(data);
  target->window->navigate(target->page);
}

void MainWindow::refresh_chrome() {
  const svc::QuotaStatus q = service_.quota();
  const std::string secrets = "机密信息 " + std::to_string(q.secrets) + " / " +
                              std::to_string(q.max_secrets);
  const std::string keys = "主密钥 " + std::to_string(q.master_keys) + " / " +
                           std::to_string(q.max_master_keys);
  quota_secrets_->copy_label(secrets.c_str());
  quota_keys_->copy_label(keys.c_str());
  quota_secret_ratio_ = q.max_secrets == 0
                            ? 0.0
                            : static_cast<double>(q.secrets) / static_cast<double>(q.max_secrets);
  quota_key_ratio_ = q.max_master_keys == 0
                         ? 0.0
                         : static_cast<double>(q.master_keys) /
                               static_cast<double>(q.max_master_keys);
  quota_secrets_->redraw();
  quota_keys_->redraw();

  // 页脚胶囊：已解锁 / 已锁定。
  const bool unlocked = service_.is_unlocked();
  state_pill_->copy_label(unlocked ? "已解锁" : "已锁定");
  state_pill_->redraw();
}

void MainWindow::paint_sidebar_decor() {
  // 侧栏底色。
  fl_color(theme::kSidebarBg);
  fl_rectf(0, 0, theme::kSidebarWidth, h());

  // 品牌（02 号画板）：盾牌 30px + 密匣 fs22 白 + SecretKeeper fs10。
  widgets::draw_icon(widgets::Icon::kKey, 20, 32, theme::kSidebarActiveText);
  fl_color(0xFFFFFF00);
  fl_font(theme::font_for(theme::kWeightRegular), 22);
  fl_draw("密匣", 60, 28, 120, 27, FL_ALIGN_LEFT);
  fl_color(theme::kSidebarIcon);
  fl_font(theme::font_for(theme::kWeightRegular), 10);
  fl_draw("SecretKeeper", 60, 55, 130, 13, FL_ALIGN_LEFT);

  // 容量卡：描边框 + 机密信息进度条（02 号画板最终稿只有一根）。
  theme::stroke_rounded(kQuotaCardX, kQuotaCardY, kQuotaCardW, kQuotaCardH, 8,
                        theme::kSidebarBorder);
  const int track_x = kQuotaCardX + 12;
  theme::fill_rounded(track_x, kQuotaCardY + 65, 144, 3, 2, theme::kProgressTrack);
  const int fill1 = static_cast<int>(144 * std::clamp(quota_secret_ratio_, 0.0, 1.0));
  if (fill1 > 0) {
    theme::fill_rounded(track_x, kQuotaCardY + 65, fill1, 3, 2, theme::kSidebarActiveText);
  }
}

void MainWindow::draw() {
  // 不在窗口层画任何装饰：内容页的 draw() 先画侧栏装饰，
  // chrome 子控件（导航按钮等）随后自然叠在其上。
  Fl_Double_Window::draw();
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

  // 窗口高度随页面切换（各画板高度不同），侧栏装饰与 chrome 位置随之摆放。
  const int w = theme::kWinW;
  const int h = page_height(page);
  if (w != this->w() || h != this->h()) resize(x(), y(), w, h);

  const bool chrome = page != Page::kUnlock;
  const int lock_y = h - kLockFromBottom;
  const int footer_y = h - kFooterFromBottom;
  lock_btn_->position(kNavX, lock_y);
  status_->position(kNavX, h - kStatusFromBottom);
  state_pill_->position(theme::kWinW - theme::kContentPad - 49, footer_y);
  footer_text_->position(theme::kSidebarWidth + theme::kContentPad, footer_y + 2);
  lock_btn_->show();
  // 05 号画板：白卡一直延伸到窗口底，设计稿没有页脚行，为按钮让位。
  const bool footer = chrome && page != Page::kExportKey;
  if (footer) {
    footer_text_->show();
  } else {
    footer_text_->hide();
  }
  state_pill_->set_shown(footer);

  // 全部页面都是本窗口的子控件，按需显示。
  for (Fl_Group* p : all_pages()) p->hide();

  Fl_Group* active = nullptr;
  switch (page) {
    case Page::kUnlock: active = unlock_.get(); break;
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

  // chrome 的显隐与页脚文本：解锁页整屏自绘，无侧栏。
  // 导航高亮：子页面归并到所属管理页（文件导入有自己的导航项）。
  Page menu_page = page;
  switch (page) {
    case Page::kCreateKey:
    case Page::kImportKey:
    case Page::kExportKey:
      menu_page = Page::kMasterKeys;
      break;
    case Page::kAddSecret:
      menu_page = Page::kSecrets;
      break;
    default:
      break;
  }
  for (int i = 0; i < kNavCount; ++i) {
    if (chrome) {
      menu_items_[i]->show();
    } else {
      menu_items_[i]->hide();
    }
    menu_items_[i]->set_selected(menu_targets_[i].page == menu_page);
    menu_items_[i]->redraw();
  }
  if (chrome) {
    quota_version_->show();
    quota_secrets_->show();
    quota_keys_->show();
  } else {
    lock_btn_->hide();
    status_->hide();
    quota_version_->hide();
    quota_secrets_->hide();
    quota_keys_->hide();
  }
  refresh_chrome();

  active->show();
  active->redraw();

  // 刷新数据：子页面切回时也要 reload，否则显示旧值。
  switch (page) {
    case Page::kUnlock:
      unlock_->reload();
      break;
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

  redraw();
}

void MainWindow::set_status(const std::string& text) {
  status_text_ = text;
  if (status_ == nullptr) return;
  if (text.empty()) {
    status_->hide();
  } else {
    status_->copy_label(text.c_str());
    status_->show();
  }
  status_->redraw();
}

void MainWindow::show_error(svc::Error e) { set_status(error_text(e)); }

void MainWindow::refresh() {
  keys_->reload();
  secrets_->reload();
  settings_page_->reload();
  refresh_chrome();
}

void MainWindow::note_activity() {
  last_activity_ = Clock::now();
  service_.notify_activity();
}

void MainWindow::on_unlocked() {
  hide_plaintext();
  refresh_chrome();
  navigate(Page::kSecrets);
}

void MainWindow::lock_now() {
  service_.lock();
  hide_plaintext();
  navigate(Page::kUnlock);
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
