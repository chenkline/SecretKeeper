#pragma once

// SecretKeeper - 页面层
//
// 依据 docs/ui/svg/ 下九张桌面端设计稿组织：
//   01 解锁与锁定 / 02 主密钥管理 / 03 生成主密钥 / 04 导入主密钥
//   05 导出主密钥 / 06 机密信息管理 / 07 新增机密信息 / 08 导入机密信息
//   09 安全设置
//
// 左侧主菜单固定三项：主密钥管理 -> 机密信息管理 -> 安全设置。
// 导入 / 导出不是主菜单项，入口在各自的管理页内（02 / 06 号设计稿）。
//
// 页面只与 service 层和 view_model 层打交道，不接触密钥材料。

#include <FL/Fl_Double_Window.H>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "theme.h"
#include "widgets.h"

namespace secretkeeper::ui {

// ---- 页面标识 ----
enum class Page {
  kMasterKeys,      // 02 主密钥管理
  kSecrets,         // 06 机密信息管理
  kSettings,        // 09 安全设置
  kCreateKey,       // 03 生成主密钥
  kImportKey,       // 04 导入主密钥
  kExportKey,       // 05 导出主密钥
  kAddSecret,       // 07 新增机密信息
  kImportSecret,    // 08 导入机密信息
};

// ---- 应用设置（09 安全设置页）----
struct SecuritySettings {
  int idle_lock_minutes = 5;      // 闲置自动锁定：1 / 5 / 15
  int clipboard_clear_seconds = 30;  // 剪贴板清除：15 / 30 / 60
  bool hide_plaintext_on_blur = true;  // 离开窗口隐藏明文
};

class MainWindow;

// ---- 单个页面基类 ----
class PageBase : public Fl_Group {
 public:
  PageBase(MainWindow* host, const char* title, const char* subtitle);
  ~PageBase() override = default;

  // 页面需要重新读取 service 数据时调用（切换到该页、切默认、增删改之后）。
  virtual void reload() {}
  // 页面状态栏文案。
  virtual std::string status_text() const { return {}; }
  // 「返回」按钮的统一处理：回到该子页面所属的管理页。
  void on_back();

 protected:
  // 顶部大标题 + 副标题的公共外壳。调用方在其下方继续摆内容。
  void layout_header(const char* back_label);

  MainWindow* host() const { return host_; }
  // PageBase 供各页调用 ask_password 等需要主窗口上下文的场合。
  MainWindow* host_window() const { return host_; }

 private:
  MainWindow* host_ = nullptr;
  std::string subtitle_;
  Fl_Box* title_box_ = nullptr;
  Fl_Box* subtitle_box_ = nullptr;
  widgets::Button* back_ = nullptr;
};

// =====================================================================
// 01 解锁与锁定（设计稿 01）
//
// 左侧是品牌区（Logo、标语），右侧是解锁表单：状态胶囊、主密钥
// 选择器、主密钥密码、解锁按钮、错误提示。
// =====================================================================
class UnlockPage : public Fl_Group {
 public:
  explicit UnlockPage(MainWindow* host);
  // 解锁页不使用 PageBase 的标题外壳，故不写 override。
  void reload();
  std::string status_text() const;

 protected:
  void draw() override;
  int handle(int event) override;

 private:
  void on_unlock();
  void on_pick_key();

  MainWindow* host_;
  widgets::Pill* state_pill_ = nullptr;
  widgets::KeySelector* selector_ = nullptr;
  widgets::Field* password_ = nullptr;
  widgets::IconButton* unlock_btn_ = nullptr;
  Fl_Box* error_ = nullptr;
  std::string selected_key_id_;
  bool unlocked_ = false;
  bool handle_event(int event);
};

// =====================================================================
// 02 主密钥管理（设计稿 02）
//
// 左列是主密钥卡片列表（名称 + ID + 关联条数 + 导入时间），
// 右列是选中主密钥的详情与操作（导出主密钥 / 删除主密钥 / 切换默认）。
// =====================================================================
class MasterKeysPage : public PageBase {
 public:
  explicit MasterKeysPage(MainWindow* host);
  void reload() override;
  std::string status_text() const override;

 protected:
  void draw() override;

 private:
  void build_key_cards();
  void on_card_click(int index);
  void on_generate();
  void on_import();
  void on_export();
  void on_delete();
  void on_switch_default();

  widgets::Notice* quota_notice_ = nullptr;
  Fl_Box* card_box_ = nullptr;
  Fl_Group* card_group_ = nullptr;
  widgets::IconButton* gen_btn_ = nullptr;
  widgets::IconButton* imp_btn_ = nullptr;
  widgets::IconButton* exp_btn_ = nullptr;
  widgets::IconButton* del_btn_ = nullptr;
  widgets::IconButton* switch_btn_ = nullptr;
  Fl_Box* detail_id_ = nullptr;
  Fl_Box* detail_name_ = nullptr;
  Fl_Box* detail_alg_ = nullptr;
  Fl_Box* detail_count_ = nullptr;
  Fl_Box* detail_created_ = nullptr;
  widgets::Pill* default_pill_ = nullptr;
  std::vector<std::string> ids_;
  int selected_ = -1;
  bool show_created_ = false;
};

// =====================================================================
// 03 生成主密钥（设计稿 03）
// =====================================================================
class CreateKeyPage : public PageBase {
 public:
  explicit CreateKeyPage(MainWindow* host);
  void reload() override;
  std::string status_text() const override;

 private:
  void on_save();

  widgets::Field* name_ = nullptr;
  widgets::Field* password_ = nullptr;
  widgets::Field* confirm_ = nullptr;
  widgets::Notice* warning_ = nullptr;
  widgets::Notice* capacity_ = nullptr;
  widgets::IconButton* save_ = nullptr;
  std::string last_error_;
};

// =====================================================================
// 04 导入主密钥（设计稿 04）
// =====================================================================
class ImportKeyPage : public PageBase {
 public:
  explicit ImportKeyPage(MainWindow* host);
  void reload() override;
  std::string status_text() const override;

 private:
  void on_choose_file();
  void on_import();

  Fl_Box* file_name_ = nullptr;
  Fl_Box* file_sub_ = nullptr;
  widgets::Button* choose_btn_ = nullptr;
  widgets::Field* protection_ = nullptr;
  widgets::Field* new_password_ = nullptr;
  widgets::IconButton* import_btn_ = nullptr;
  std::string path_;
  std::string last_error_;
};

// =====================================================================
// 05 导出主密钥（设计稿 05）
// =====================================================================
class ExportKeyPage : public PageBase {
 public:
  explicit ExportKeyPage(MainWindow* host);
  void reload() override;
  std::string status_text() const override;

 private:
  void on_choose_location();
  void on_export();

  Fl_Box* key_name_box_ = nullptr;
  Fl_Box* key_id_box_ = nullptr;
  widgets::Field* original_ = nullptr;
  widgets::Field* protection_ = nullptr;
  widgets::Field* confirm_ = nullptr;
  Fl_Box* location_box_ = nullptr;
  widgets::Button* choose_btn_ = nullptr;
  widgets::Notice* warning_ = nullptr;
  widgets::Notice* info_ = nullptr;
  widgets::IconButton* export_btn_ = nullptr;
  std::string key_id_;
  std::string location_;
  std::string last_error_;
};

// =====================================================================
// 06 机密信息管理（设计稿 06）
//
// 左列是机密信息列表（标题 / 主密钥 ID / 主密钥名称），搜索框在上方，
// 「导入」与「新增机密信息」并排在标题栏右侧；右列是信息详情，
// 含复制、导出、删除三个操作与隐藏明文的开关。
// =====================================================================
class SecretsPage : public PageBase {
 public:
  explicit SecretsPage(MainWindow* host);
  void reload() override;
  std::string status_text() const override;
  // 离开窗口 / 切页时立即重新掩码（需求 4.4）。由 MainWindow 调用。
  void hide_plaintext();

 protected:
  void draw() override;

 private:
  void on_row_select();
  void on_add();
  void on_import();
  void on_copy();
  void on_export();
  void on_delete();
  void on_toggle_reveal();
  void refresh_detail();
  void refresh_meta();

  Fl_Box* count_label_ = nullptr;
  widgets::Field* search_ = nullptr;
  widgets::List* list_ = nullptr;
  Fl_Box* detail_title_ = nullptr;
  widgets::Pill* verified_ = nullptr;
  Fl_Box* detail_mk_id_ = nullptr;
  Fl_Box* detail_mk_name_ = nullptr;
  Fl_Box* detail_title_field_ = nullptr;
  widgets::IconButton* reveal_btn_ = nullptr;
  Fl_Box* plaintext_box_ = nullptr;
  Fl_Box* plaintext_meta_ = nullptr;
  widgets::IconButton* copy_btn_ = nullptr;
  widgets::IconButton* exp_btn_ = nullptr;
  widgets::IconButton* del_btn_ = nullptr;
  widgets::Notice* hide_notice_ = nullptr;
  widgets::IconButton* imp_btn_ = nullptr;
  widgets::IconButton* add_btn_ = nullptr;
  std::vector<std::string> ids_;
  std::string selected_id_;
  std::string plaintext_;
  bool revealed_ = false;
  std::string last_error_;
};

// =====================================================================
// 07 新增机密信息（设计稿 07）
// =====================================================================
class AddSecretPage : public PageBase {
 public:
  explicit AddSecretPage(MainWindow* host);
  void reload() override;
  std::string status_text() const override;

 private:
  void on_save();
  void on_pick_key();

  widgets::KeySelector* selector_ = nullptr;
  widgets::Field* title_ = nullptr;
  widgets::TextArea* content_ = nullptr;
  Fl_Box* counter_ = nullptr;
  widgets::Notice* info_ = nullptr;
  widgets::IconButton* save_ = nullptr;
  std::string key_id_;
  std::string last_error_;
  std::string last_status_;
};

// =====================================================================
// 08 导入机密信息（设计稿 08）
//
// 三段步骤条（文件已选择 / 验证主密钥 / 保存到本机）、文件选择区、
// 匹配到的主密钥区块（带勾或叉的状态图标）、主密钥密码框、导入按钮。
// =====================================================================
class ImportSecretPage : public PageBase {
 public:
  explicit ImportSecretPage(MainWindow* host);
  void reload() override;
  std::string status_text() const override;

 protected:
  void draw() override;

 private:
  void on_choose_file();
  void on_import();

  Fl_Box* step1_ = nullptr;
  Fl_Box* step2_ = nullptr;
  Fl_Box* step3_ = nullptr;
  Fl_Box* file_name_ = nullptr;
  Fl_Box* file_sub_ = nullptr;
  widgets::Button* choose_btn_ = nullptr;
  Fl_Box* match_title_ = nullptr;
  Fl_Box* match_icon_ = nullptr;
  Fl_Box* match_id_ = nullptr;
  Fl_Box* match_name_ = nullptr;
  Fl_Box* match_hint_ = nullptr;
  widgets::Field* key_password_ = nullptr;
  widgets::IconButton* import_btn_ = nullptr;
  std::string path_;
  std::string key_id_;
  bool key_found_ = false;
  std::string last_error_;
};

// =====================================================================
// 09 安全设置（设计稿 09）
// =====================================================================
class SettingsPage : public PageBase {
 public:
  explicit SettingsPage(MainWindow* host);
  void reload() override;
  std::string status_text() const override;

 private:
  void on_idle_choice(int minutes);
  void on_clipboard_choice(int seconds);
  void on_blur_toggle(bool on);
  void on_lock_now();

  widgets::Pill* idle_value_ = nullptr;
  widgets::Pill* clipboard_value_ = nullptr;
  widgets::Pill* blur_value_ = nullptr;
};

}  // namespace secretkeeper::ui
