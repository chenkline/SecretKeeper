#pragma once

// SecretKeeper - 主窗口
//
// 负责四件事：
//   1. 左侧导航（02-09 号画板公共：品牌、五项导航、容量卡、锁定、离线状态）；
//   2. 页面路由与按页窗口高度（各画板高度不同）；
//   3. 持有唯一的 service 实例，把页面需要的能力以只读访问器暴露出去；
//   4. 定时器：KEK 轮换、闲置锁定、剪贴板定时清除。
//
// 解锁页（01 号）没有侧栏：它的左侧是 456px 品牌承诺区。
// 页面通过 MainWindow 访问 service 与设置，不直接持有 service。

#include <FL/Fl_Double_Window.H>

#include <array>
#include <chrono>
#include <memory>
#include <string>

#include "core/service.h"
#include "pages.h"
#include "theme.h"

namespace secretkeeper::ui {

using Clock = std::chrono::steady_clock;

class MainWindow;

// FLTK 回调只能传一个 void*，用 MenuTarget 把「窗口 + 目标页」一起带过去。
struct MenuTarget {
  MainWindow* window = nullptr;
  Page page = Page::kMasterKeys;
};

class MainWindow : public Fl_Double_Window {
 public:
  MainWindow();
  ~MainWindow() override;

  // ---- 页面可用的能力（全部只读，页面不得改状态）----
  service::Service& service() { return service_; }
  const service::Service& service() const { return service_; }
  const SecuritySettings& settings() const { return settings_; }
  SecuritySettings& mutable_settings() { return settings_; }
  std::string export_target() const { return export_target_; }
  void set_export_target(std::string_view id) { export_target_.assign(id); }

  // ---- 路由 ----
  void navigate(Page page);
  // 从子页面返回：回到它所属的管理页。
  void go_back();

  // ---- 状态与事件 ----
  void set_status(const std::string& text);
  void show_error(service::Error e);
  void refresh();
  void note_activity();
  void on_unlocked();
  void lock_now();
  void arm_clipboard_clear();
  // 窗口尺寸变化时同步各页 body 视口与 chrome 位置。
  void resize(int X, int Y, int W, int H) override;

  // 绘制 02-09 号画板公共的侧栏装饰（底色、品牌、容量卡框与进度条）。
  // 各内容页在自己的 draw() 最开头调用，保证装饰位于页面内容之下、
  // 导航按钮等窗口子控件之上。
  void paint_sidebar_decor();

 protected:
  void draw() override;
  int handle(int event) override;

 private:
  void build_chrome();
  void show_page(Page page);
  void refresh_chrome();
  // 侧栏底部与页脚 chrome 随窗口尺寸摆放（show_page 与 resize 共用）。
  void layout_chrome(int win_w, int win_h);
  // 全部九个页面，按构造顺序。
  std::array<Fl_Group*, 9> all_pages() const;
  static void on_menu_cb(Fl_Widget*, void* data);
  static void on_rotate_tick(void* data);
  static void on_idle_tick(void* data);
  static void on_clipboard_clear(void* data);
  void check_idle();
  void hide_plaintext();

  service::Service service_;
  SecuritySettings settings_;

  Page current_ = Page::kUnlock;
  std::unique_ptr<UnlockPage> unlock_;
  std::unique_ptr<MasterKeysPage> keys_;
  std::unique_ptr<SecretsPage> secrets_;
  std::unique_ptr<SettingsPage> settings_page_;
  std::unique_ptr<CreateKeyPage> create_key_;
  std::unique_ptr<ImportKeyPage> import_key_;
  std::unique_ptr<ExportKeyPage> export_key_;
  std::unique_ptr<AddSecretPage> add_secret_;
  std::unique_ptr<ImportSecretPage> import_secret_;

  // ---- 侧栏 chrome（02-09 号画板公共，解锁页整体隐藏）----
  static constexpr int kNavCount = 3;
  MenuTarget menu_targets_[kNavCount];
  widgets::IconButton* menu_items_[kNavCount] = {};
  widgets::Button* lock_btn_ = nullptr;
  Fl_Box* quota_version_ = nullptr;
  Fl_Box* quota_secrets_ = nullptr;
  Fl_Box* quota_keys_ = nullptr;
  Fl_Box* status_ = nullptr;
  // 内容页页脚（本机加密保存 · 无网络连接 · 无云同步 + 锁定状态胶囊）。
  Fl_Box* footer_text_ = nullptr;
  widgets::Pill* state_pill_ = nullptr;

  double quota_secret_ratio_ = 0.0;
  double quota_key_ratio_ = 0.0;

  std::string export_target_;
  std::string status_text_;
  bool clipboard_armed_ = false;
  bool initialized_ = false;   // 构造完成前忽略 resize 同步
  bool resizing_for_page_ = false;  // show_page 内部的整窗调整不算用户缩放
  bool user_resized_ = false;  // 用户手动调整过窗口后不再按页改窗口高度
  Clock::time_point last_activity_ = Clock::now();
};

}  // namespace secretkeeper::ui
