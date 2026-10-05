#pragma once

// SecretKeeper - 自绘控件层
//
// FLTK 内置控件是 1990 年代外观（灰底方角），与设计稿的圆角卡片、
// 主色按钮、胶囊标签差距很大，因此这里按 docs/ui/UI设计规范.md 自绘。
//
// 每个控件都只负责「画」与「回报事件」，不含任何业务判断：
// 业务逻辑在 pages.cpp，展示规则（黄色问号、文案）在 view_model.cpp。

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Widget.H>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "theme.h"

namespace secretkeeper::ui::widgets {

// ---- 卡片 ----
// 白底圆角容器。设计稿里所有内容区块都是它。
class Card : public Fl_Widget {
 public:
  Card(int x, int y, int w, int h);
  // 自定义填充色（设计稿里的浅底块）。radius <= 0 时画直角。
  Card(int x, int y, int w, int h, Fl_Color bg);
  void draw() override;
  int handle(int event) override;

 private:
  Fl_Color bg_ = 0xFFFFFF;
  bool custom_bg_ = false;
};

// ---- 主色按钮 ----
// 填充分三种：主色实心、次要（白底描边）、危险（红字红边）。
enum class ButtonKind { kPrimary, kSecondary, kDanger };

class Button : public Fl_Button {
 public:
  Button(int x, int y, int w, int h, std::string_view label,
         ButtonKind kind = ButtonKind::kPrimary);
  void draw() override;
  void set_enabled(bool on);
  bool enabled() const { return enabled_; }
  // 侧栏菜单高亮：选中项用主色浅底与主色文字。
  void set_selected(bool on);
  bool selected() const { return selected_; }

 protected:
  int handle(int event) override;
  // IconButton 在自己的 draw 里需要复用 Button 的配色判定。
  ButtonKind kind() const { return kind_; }
  bool is_enabled() const { return enabled_; }
  bool is_selected() const { return selected_; }

 private:
  ButtonKind kind_;
  bool enabled_ = true;
  bool selected_ = false;
};

// ---- 标签页 / 步骤胶囊 ----
// 设计稿里解锁页的「已锁定」与导入页的三段步骤条都用它。
class Pill : public Fl_Widget {
 public:
  Pill(int x, int y, int w, int h, std::string_view text, Fl_Color fg, Fl_Color bg);
  void draw() override;
  // 设置页的可选项用它做点击目标。
  void set_active(bool on) { active_ = on; }

 protected:
  int handle(int event) override;

 private:
  bool active_ = false;
  std::string text_;
  Fl_Color fg_;
  Fl_Color bg_;
};

// ---- 提示条 ----
// 黄色（提醒）/ 绿色（说明）/ 红色（错误）三种，标题加正文两行。
class Notice : public Fl_Widget {
 public:
  enum class Tone { kWarning, kInfo, kSuccess, kDanger };
  Notice(int x, int y, int w, int h, Tone tone);
  // title 与 body 换行自适应高度；调用 set_content 后需 resize。
  void set_content(std::string_view title, std::string_view body);
  int preferred_height() const;
  void draw() override;

 private:
  Tone tone_;
  std::string title_;
  std::string body_;
};

// ---- 文本输入框 ----
// 白底圆角 + 可选前置标签。支持密码掩码与占位符。
class Field : public Fl_Input {
 public:
  Field(int x, int y, int w, int h, std::string_view label = {});
  void draw() override;
  // 占位符：无输入时以浅灰显示，不进入 value()。
  void set_placeholder(std::string_view text);
  void set_label_text(std::string_view text);
  std::string label_text() const { return label_; }

 protected:
  int handle(int event) override;

 private:
  std::string label_;
  std::string placeholder_;
};

// ---- 多行文本域 ----
// 机密信息内容用。带边框圆角，高度固定，内部可滚动。
class TextArea : public Fl_Input {
 public:
  TextArea(int x, int y, int w, int h);
  void draw() override;
};

// ---- 列表 ----
// 替代 FLTK 表格：设计稿的列表是圆角卡片内的分隔行，无网格线。
// 每行三列，行高固定，选中行浅绿底。缺失主密钥的行在 ID 列尾画黄色问号。
struct ListRow {
  std::string c0;
  std::string c1;
  std::string c2;
  bool show_question_mark = false;
};

class List : public Fl_Widget {
 public:
  List(int x, int y, int w, int h);
  void set_columns(std::vector<std::string> headers);
  void set_rows(std::vector<ListRow> rows);
  // 点击命中行号；未命中返回 -1。
  int row_at(int px, int py) const;
  int selected() const { return selected_; }
  void select(int row);
  void clear_selection();

 protected:
  void draw() override;
  int handle(int event) override;

 private:
  int header_height() const { return 36; }
  int row_height() const { return 44; }
  void layout_columns();

  std::vector<std::string> headers_;
  std::vector<ListRow> rows_;
  int selected_ = -1;
  int col_x_[3] = {0, 0, 0};
  int col_w_[3] = {0, 0, 0};
};

// ---- 图标 ----
// 设计稿用 Lucide 线性图标。这里内置所需的少数几个（钥匙、文件、
// 设置、导入、导出、加号、锁、眼睛、勾、叉、搜索、文件夹、告警）。
// 均为矢量路径绘制，非位图。
enum class Icon {
  kKey, kFile, kSettings, kImport, kExport, kPlus, kLock,
  kEye, kEyeOff, kCheck, kCross, kSearch, kFolder, kWarning, kInfo, kTrash, kCopy,
};

// 在 (x,y) 处画一个 20x20 的图标，color 为描边色。
void draw_icon(Icon icon, int x, int y, Fl_Color color);

// 图标实际占用的边长（当前所有图标统一 20）。
constexpr int kIconSize = 20;

// ---- 带图标的按钮 ----
// 设计稿的主按钮左侧带图标（如「保存」「导出主密钥」）。
class IconButton : public Button {
 public:
  IconButton(int x, int y, int w, int h, Icon icon, std::string_view label,
             ButtonKind kind = ButtonKind::kPrimary);
  void draw() override;

 private:
  Icon icon_;
};

// ---- 主密钥选择器 ----
// 解锁页与新增机密信息页共用：一个可点击的圆角行，内含钥匙图标、
// 名称、主密钥 ID、状态标签与下拉箭头。点击展开选择列表由调用方处理。
class KeySelector : public Fl_Widget {
 public:
  KeySelector(int x, int y, int w, int h);
  void set_key(std::string_view name, std::string_view id, bool is_default);
  void set_placeholder(std::string_view text);
  void draw() override;
  int handle(int event) override;

 private:
  std::string name_;
  std::string id_;
  bool is_default_ = false;
  std::string placeholder_;
};

}  // namespace secretkeeper::ui::widgets
