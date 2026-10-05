#pragma once

// SecretKeeper - 主窗口
//
// 负责三件事：
//   1. 左侧主菜单（三项：主密钥管理 / 机密信息管理 / 安全设置）与页面路由；
//   2. 持有唯一的 service 实例，把所有页面需要的能力以只读访问器暴露出去；
//   3. 定时器：KEK 轮换、闲置锁定、剪贴板定时清除。
//
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

// 见下方 build_menu 的注释：FLTK 回调只能传一个 void*。
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

 protected:
  void draw() override;
  int handle(int event) override;

 private:
  void build_menu();
  void show_page(Page page);
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

  Page current_ = Page::kMasterKeys;
  std::unique_ptr<UnlockPage> unlock_;
  std::unique_ptr<MasterKeysPage> keys_;
  std::unique_ptr<SecretsPage> secrets_;
  std::unique_ptr<SettingsPage> settings_page_;
  std::unique_ptr<CreateKeyPage> create_key_;
  std::unique_ptr<ImportKeyPage> import_key_;
  std::unique_ptr<ExportKeyPage> export_key_;
  std::unique_ptr<AddSecretPage> add_secret_;
  std::unique_ptr<ImportSecretPage> import_secret_;

  // FLTK 回调只能传一个 void*，用 MenuTarget 把「窗口 + 目标页」一起带过去。
  MenuTarget menu_targets_[3];
  widgets::Button* menu_items_[3] = {nullptr, nullptr, nullptr};
  Fl_Box* status_ = nullptr;
  widgets::IconButton* lock_btn_ = nullptr;

  std::string export_target_;
  std::string status_text_;
  bool clipboard_armed_ = false;
  Clock::time_point last_activity_ = Clock::now();
};

}  // namespace secretkeeper::ui
