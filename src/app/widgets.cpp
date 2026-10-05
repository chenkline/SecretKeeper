#include "widgets.h"

#include <FL/fl_draw.H>

#include <algorithm>
#include <cmath>

namespace secretkeeper::ui::widgets {
namespace {

// 主色按钮的悬浮态取设计稿里的 #3D6B6D。
constexpr Fl_Color kPrimaryHover = theme::kAccentHover;

void draw_label_centered(const std::string& text, int font_weight, int font_size,
                         Fl_Color fg, int x, int y, int width, int height) {
  fl_color(fg);
  fl_font(theme::font_for(font_weight), font_size);
  fl_draw(text.c_str(), x, y, width, height, FL_ALIGN_CENTER);
}

// 按像素宽度断行。FLTK 自带 fl_draw 的换行能力，但需要 Fl_Widget 上下文，
// 这里自己做一次按字符测量，保证提示条里的中文能正确折行。
std::vector<std::string> wrap_by_width(std::string_view text, int font_weight,
                                        int font_size, int max_width) {
  std::vector<std::string> lines;
  if (text.empty()) return lines;
  fl_font(theme::font_for(font_weight), font_size);
  std::string current;
  int current_width = 0;
  for (char ch : text) {
    if (ch == '\n') {
      lines.push_back(current);
      current.clear();
      current_width = 0;
      continue;
    }
    const int char_width = static_cast<int>(fl_width(ch));
    // 允许行尾溢出 4px，避免中文因标点宽度差异频繁折行。
    if (current_width + char_width > max_width + 4 && !current.empty()) {
      lines.push_back(current);
      current.clear();
      current_width = 0;
    }
    current.push_back(ch);
    current_width += char_width;
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}

}  // namespace

// =====================================================================
Card::Card(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {
  box(FL_NO_BOX);
}

Card::Card(int x, int y, int w, int h, Fl_Color bg) : Card(x, y, w, h) {
  bg_ = bg;
  custom_bg_ = true;
}

void Card::draw() {
  theme::fill_rounded(x(), y(), w(), h(), theme::kRadiusCard,
                      custom_bg_ ? bg_ : theme::kCardBg);
  if (!custom_bg_) {
    theme::stroke_rounded(x(), y(), w(), h(), theme::kRadiusCard, theme::kBorder);
  }
}

int Card::handle(int event) {
  // 卡片只是背景，必须让点击穿透到上层控件，否则会吃掉列表的点击。
  return Fl_Widget::handle(event);
}

// =====================================================================
Button::Button(int x, int y, int w, int h, std::string_view label, ButtonKind kind)
    : Fl_Button(x, y, w, h, 0), kind_(kind) {
  // string_view::data() 不保证以 NUL 结尾，必须拷贝后交给 FLTK。
  label_ = std::string(label);
  copy_label(label_.c_str());
  box(FL_NO_BOX);
  down_box(FL_NO_BOX);
  labelsize(theme::kFontBody);
}

void Button::set_label(std::string_view text) {
  label_ = std::string(text);
  Fl_Button::copy_label(label_.c_str());
  redraw();
}

void Button::set_selected(bool on) {
  if (selected_ == on) return;
  selected_ = on;
  redraw();
}

void Button::set_enabled(bool on) {
  if (enabled_ == on) return;
  enabled_ = on;
  redraw();
}

void Button::draw() {
  Fl_Color bg = theme::kCardBg;
  Fl_Color fg = theme::kText;
  Fl_Color border = theme::kBorder;

  switch (kind_) {
    case ButtonKind::kPrimary:
      bg = (active() && enabled_) ? kPrimaryHover : theme::kAccent;
      fg = theme::kOnAccent;
      border = bg;
      break;
    case ButtonKind::kDanger:
      bg = theme::kDangerBg;
      fg = enabled_ ? theme::kDangerText : theme::kTextFaint;
      border = theme::kDangerBorder;
      break;
    case ButtonKind::kSecondary:
    default:
      bg = theme::kCardBg;
      fg = enabled_ ? theme::kText : theme::kTextFaint;
      border = theme::kBorder;
      break;
  }

  if (!enabled_) {
    // 禁用态统一降到浅灰底，忽略 kind 的配色。
    bg = theme::kSubtleBg;
    border = theme::kBorder;
  }
  if (selected_) {
    // 侧栏选中项：主色浅底 + 主色文字（设计稿左侧菜单）。
    bg = theme::kSidebarActiveBg;
    fg = theme::kSidebarActiveText;
    border = bg;
  }

  theme::fill_rounded(x(), y(), w(), h(), theme::kRadiusButton, bg);
  if (kind_ != ButtonKind::kPrimary) {
    theme::stroke_rounded(x(), y(), w(), h(), theme::kRadiusButton, border);
  }
  draw_label_centered(label_, theme::kWeightMedium, theme::kFontBody, fg, x(), y(), w(), h());
}

int Button::handle(int event) {
  if (!enabled_ && event == FL_PUSH) return 1;
  return Fl_Button::handle(event);
}

// =====================================================================
Pill::Pill(int x, int y, int w, int h, std::string_view text, Fl_Color fg, Fl_Color bg)
    : Fl_Widget(x, y, w, h), text_(text), fg_(fg), bg_(bg) {
  box(FL_NO_BOX);
  when(FL_WHEN_RELEASE);
}

int Pill::handle(int event) {
  if (!shown_) return Fl_Widget::handle(event);
  if (event == FL_PUSH) {
    do_callback();
    return 1;
  }
  return Fl_Widget::handle(event);
}

void Pill::draw() {
  if (!shown_) return;
  theme::fill_rounded(x(), y(), w(), h(), h() / 2, bg_);
  draw_label_centered(text_, theme::kWeightMedium, theme::kFontTiny, fg_, x(), y(), w(),
                      h());
}

// =====================================================================
Notice::Notice(int x, int y, int w, int h, Tone tone) : Fl_Widget(x, y, w, h), tone_(tone) {
  box(FL_NO_BOX);
}

void Notice::set_content(std::string_view title, std::string_view body) {
  title_.assign(title);
  body_.assign(body);
  redraw();
}

int Notice::preferred_height() const {
  const int pad = theme::kGapLg;
  int height = pad;
  if (!title_.empty()) height += theme::kFontLabel + theme::kGapSm;
  const auto lines = wrap_by_width(body_, theme::kWeightRegular, theme::kFontSmall, w() - pad * 2);
  height += static_cast<int>(lines.size()) * (theme::kFontSmall + 4);
  return height + pad;
}

void Notice::draw() {
  if (!shown_) return;
  Fl_Color bg = theme::kAccentSoft;
  Fl_Color fg = theme::kAccent;
  Fl_Color strong = theme::kText;
  switch (tone_) {
    case Tone::kWarning:
      bg = theme::kWarningBg;
      fg = theme::kWarningText;
      strong = theme::kWarningText;
      break;
    case Tone::kDanger:
      bg = theme::kDangerBg;
      fg = theme::kDangerText;
      strong = theme::kDangerText;
      break;
    case Tone::kInfo:
    case Tone::kSuccess:
    default:
      bg = theme::kAccentSoft;
      fg = theme::kAccent;
      strong = theme::kAccent;
      break;
  }
  theme::fill_rounded(x(), y(), w(), h(), theme::kRadiusCard, bg);

  const int pad = theme::kGapLg;
  const int icon_x = x() + pad;
  const int text_x = x() + pad + kIconSize + theme::kGapMd;
  const int text_w = w() - (text_x - x()) - pad;
  int cursor = y() + pad;

  draw_icon(Icon::kWarning, icon_x, cursor, fg);
  if (tone_ == Tone::kInfo || tone_ == Tone::kSuccess) {
    draw_icon(Icon::kInfo, icon_x, cursor, fg);
  }
  if (tone_ == Tone::kDanger) {
    draw_icon(Icon::kCross, icon_x, cursor, fg);
  }

  if (!title_.empty()) {
    fl_color(strong);
    fl_font(theme::font_for(theme::kWeightBold), theme::kFontLabel);
    fl_draw(title_.c_str(), text_x, cursor, text_w, theme::kFontLabel + 4, FL_ALIGN_LEFT);
    cursor += theme::kFontLabel + theme::kGapSm;
  }
  fl_color(strong);
  fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
  for (const std::string& line :
       wrap_by_width(body_, theme::kWeightRegular, theme::kFontSmall, text_w)) {
    fl_draw(line.c_str(), text_x, cursor, text_w, theme::kFontSmall + 4, FL_ALIGN_LEFT);
    cursor += theme::kFontSmall + 4;
  }
}

// =====================================================================
Field::Field(int x, int y, int w, int h, std::string_view label)
    : Fl_Input(x, y, w, h, 0) {
  label_.assign(label);
  box(FL_NO_BOX);
  labelsize(theme::kFontBody);
  Fl_Input::type(FL_NORMAL_INPUT);
}

void Field::set_placeholder(std::string_view text) {
  placeholder_.assign(text);
  redraw();
}

void Field::set_label_text(std::string_view text) {
  label_.assign(text);
  redraw();
}

void Field::draw() {
  theme::fill_rounded(x(), y(), w(), h(), theme::kRadiusField, theme::kFieldBg);
  theme::stroke_rounded(x(), y(), w(), h(), theme::kRadiusField, theme::kBorder);

  // 内容区左侧留 12px 内边距，右侧留 12px（给眼睛图标留位）。
  Fl_Input::box(FL_NO_BOX);
  Fl_Input::labelsize(theme::kFontBody);

  const char* value = Fl_Input::value();
  const bool empty = value == nullptr || value[0] == '\0';
  if (empty && !placeholder_.empty()) {
    fl_color(theme::kTextFaint);
    fl_font(theme::font_for(theme::kWeightRegular), theme::kFontBody);
    fl_draw(placeholder_.c_str(), x() + theme::kGapMd, y(), w() - theme::kGapMd * 2, h(),
            FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  }

}

int Field::handle(int event) {
  return Fl_Input::handle(event);
}

// =====================================================================
TextArea::TextArea(int x, int y, int w, int h) : Fl_Input(x, y, w, h, 0) {
  box(FL_NO_BOX);
  textfont(theme::font_for(theme::kWeightRegular));
  textsize(theme::kFontBody);
  align(FL_ALIGN_TOP | FL_ALIGN_LEFT);
  type(FL_MULTILINE_INPUT);
  // FLTK 的 Fl_Input 在 type 为 MULTILINE 时才会显示滚动条。
  when(FL_WHEN_CHANGED);
}

void TextArea::draw() {
  theme::fill_rounded(x(), y(), w(), h(), theme::kRadiusField, theme::kFieldBg);
  theme::stroke_rounded(x(), y(), w(), h(), theme::kRadiusField, theme::kBorder);
  // 让 Fl_Input 绘制实际文本，偏移 8px 内边距。
  Fl_Input::box(FL_NO_BOX);
}

// =====================================================================
List::List(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {
  box(FL_NO_BOX);
  when(FL_WHEN_RELEASE);
}

void List::set_columns(std::vector<std::string> headers) {
  headers_ = std::move(headers);
  while (headers_.size() < 3) headers_.emplace_back();
  headers_.resize(3);
  layout_columns();
  redraw();
}

void List::set_rows(std::vector<ListRow> rows) {
  rows_ = std::move(rows);
  if (selected_ >= static_cast<int>(rows_.size())) selected_ = -1;
  redraw();
}

void List::layout_columns() {
  // 设计稿里第一列（标题 / 名称）最宽，主密钥 ID 次之，名称列收窄。
  const int usable = w() - theme::kGapLg * 2;
  col_w_[0] = usable * 45 / 100;
  col_w_[1] = usable * 28 / 100;
  col_w_[2] = usable - col_w_[0] - col_w_[1];
  col_x_[0] = x() + theme::kGapLg;
  col_x_[1] = col_x_[0] + col_w_[0];
  col_x_[2] = col_x_[1] + col_w_[1];
}

int List::row_at(int px, int py) const {
  if (px < x() || px > x() + w() || py < y() + header_height() || py > y() + h()) return -1;
  const int index = (py - y() - header_height()) / row_height();
  if (index < 0 || index >= static_cast<int>(rows_.size())) return -1;
  return index;
}

void List::select(int row) {
  if (row < -1 || row >= static_cast<int>(rows_.size())) return;
  selected_ = row;
  redraw();
}

void List::clear_selection() {
  selected_ = -1;
  redraw();
}

int List::handle(int event) {
  switch (event) {
    case FL_PUSH: {
      const int row = row_at(Fl::event_x(), Fl::event_y());
      if (row >= 0) {
        select(row);
        do_callback();
      } else {
        clear_selection();
        do_callback();
      }
      return 1;
    }
    case FL_MOVE:
    case FL_ENTER:
      return 1;
    default:
      break;
  }
  return Fl_Widget::handle(event);
}

void List::draw() {
  theme::fill_rounded(x(), y(), w(), h(), theme::kRadiusCard, theme::kCardBg);

  const int pad = theme::kGapLg;
  // 表头
  fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
  for (int c = 0; c < 3; ++c) {
    fl_color(theme::kTextMuted);
    fl_draw(headers_[static_cast<std::size_t>(c)].c_str(), col_x_[c], y(), col_w_[c],
            header_height(), FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  }

  const int list_top = y() + header_height();
  const int list_h = h() - header_height();
  fl_push_clip(x(), list_top, w(), list_h);

  const int rows_visible = std::max(0, list_h / row_height());
  for (int r = 0; r < rows_visible && r < static_cast<int>(rows_.size()); ++r) {
    const ListRow& row = rows_[static_cast<std::size_t>(r)];
    const int ry = list_top + r * row_height();
    if (r == selected_) {
      theme::fill_rounded(x() + 1, ry, w() - 2, row_height(), 0, theme::kAccentSoft);
    }
    const std::string* cells[3] = {&row.c0, &row.c1, &row.c2};
    for (int c = 0; c < 3; ++c) {
      const int marker_slot = (c == 1 && row.show_question_mark) ? theme::kGapLg : 0;
      fl_color(theme::kText);
      fl_font(theme::font_for(theme::kWeightRegular), theme::kFontBody);
      fl_draw(cells[c]->c_str(), col_x_[c], ry, col_w_[c] - marker_slot, row_height(),
              FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
      if (c == 1 && row.show_question_mark) {
        // 黄色问号紧跟 ID，属于 ID 列列值的一部分，
        // 与名称列是否为空无关（需求 3.1 / 3.5）。
        fl_color(theme::kWarningBorder);
        fl_font(theme::font_for(theme::kWeightBold), theme::kFontBody);
        fl_draw("?", col_x_[c] + col_w_[c] - marker_slot + theme::kGapXs, ry, marker_slot,
                row_height(), FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
      }
    }
    if (r + 1 < static_cast<int>(rows_.size())) {
      fl_color(theme::kBorderSubtle);
      fl_rect(x() + pad, ry + row_height() - 1, w() - pad * 2, 1);
    }
  }
  fl_pop_clip();
}

// =====================================================================
IconButton::IconButton(int x, int y, int w, int h, Icon icon, std::string_view label,
                       ButtonKind kind)
    : Button(x, y, w, h, label, kind), icon_(icon) {}

void IconButton::draw() {
  Button::draw();
  // 图标画在文字左侧，整体（图标 + 文字）仍以按钮中心对齐。
  fl_font(theme::font_for(theme::kWeightMedium), theme::kFontBody);
  const int text_w = static_cast<int>(fl_width(label_text().c_str()));
  const int gap = theme::kGapSm;
  const int total = kIconSize + gap + text_w;
  const int start_x = x() + (w() - total) / 2;
  Fl_Color fg = kind() == ButtonKind::kPrimary ? theme::kOnAccent : theme::kText;
  if (!is_enabled()) fg = theme::kTextFaint;
  draw_icon(icon_, start_x, y() + (h() - kIconSize) / 2, fg);
}

// =====================================================================
KeySelector::KeySelector(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {
  box(FL_NO_BOX);
  when(FL_WHEN_RELEASE);
}

void KeySelector::set_key(std::string_view name, std::string_view id, bool is_default) {
  name_.assign(name);
  id_.assign(id);
  is_default_ = is_default;
  redraw();
}

void KeySelector::set_placeholder(std::string_view text) {
  placeholder_.assign(text);
  redraw();
}

int KeySelector::handle(int event) {
  if (event == FL_PUSH) {
    do_callback();
    return 1;
  }
  return Fl_Widget::handle(event);
}

void KeySelector::draw() {
  const bool empty = name_.empty() && id_.empty();
  theme::fill_rounded(x(), y(), w(), h(), theme::kRadiusField,
                      empty ? theme::kSubtleBg : theme::kCardBg);
  theme::stroke_rounded(x(), y(), w(), h(), theme::kRadiusField,
                       empty ? theme::kBorder : theme::kAccent);

  const int pad = theme::kGapLg;
  const int icon_x = x() + pad;
  const int text_x = icon_x + kIconSize + theme::kGapMd;
  const int chevron_w = 24;
  const int pill_w = is_default_ ? theme::pill_width("默认", theme::kFontTiny) : 0;
  const int text_w = w() - (text_x - x()) - pad - chevron_w - pill_w;

  draw_icon(Icon::kKey, icon_x, y() + (h() - kIconSize) / 2, theme::kAccent);

  if (empty) {
    fl_color(theme::kTextMuted);
    fl_font(theme::font_for(theme::kWeightRegular), theme::kFontBody);
    fl_draw(placeholder_.c_str(), text_x, y(), text_w, h(), FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  } else {
    fl_color(theme::kText);
    fl_font(theme::font_for(theme::kWeightMedium), theme::kFontBody);
    fl_draw(name_.c_str(), text_x, y(), text_w, h() / 2, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    fl_color(theme::kTextMuted);
    fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
    fl_draw(id_.c_str(), text_x, y() + h() / 2, text_w, h() / 2, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  }

  int cursor = x() + w() - pad - chevron_w;
  if (is_default_) {
    const int ph = 22;
    const int py = y() + (h() - ph) / 2;
    theme::draw_pill(cursor - theme::pill_width("默认", theme::kFontTiny), py,
                     theme::pill_width("默认", theme::kFontTiny), ph, "默认",
                     theme::kAccent, theme::kAccentSoft);
    cursor -= theme::pill_width("默认", theme::kFontTiny) + theme::kGapSm;
  }
  // 下拉箭头
  fl_color(theme::kTextMuted);
  fl_line_style(FL_SOLID, 1);
  const int cx = cursor + 8;
  const int cy = y() + h() / 2 - 2;
  fl_line(cx - 4, cy, cx, cy + 4);
  fl_line(cx, cy + 4, cx + 4, cy);
  fl_line_style(FL_SOLID, 0);
}

// =====================================================================
// 图标：全部为矢量路径，取自设计稿所用的 Lucide 线性风格（24 网格，
// 1px 描边），这里按 20x20 缩放绘制。
// =====================================================================
namespace {

void stroke_path(const int* pts, int count, Fl_Color c) {
  fl_color(c);
  fl_line_style(FL_SOLID, 1);
  for (int i = 0; i + 3 < count; i += 2) {
    fl_line(pts[i], pts[i + 1], pts[i + 2], pts[i + 3]);
  }
}

void circle(int cx, int cy, int r, Fl_Color c) {
  fl_color(c);
  fl_line_style(FL_SOLID, 1);
  fl_arc(cx, cy, r, 0, 2 * 3.14159265f);
  fl_line_style(FL_SOLID, 0);
}

}  // namespace

void draw_icon(Icon icon, int x, int y, Fl_Color color) {
  const int cx = x + kIconSize / 2;
  const int cy = y + kIconSize / 2;
  switch (icon) {
    case Icon::kKey: {
      // 钥匙：左下圆环 + 右上齿
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_arc(x + 6, y + 14, 4, 0, 2 * 3.14159265f);
      fl_line(x + 9, y + 11, x + 17, y + 3);
      fl_line(x + 14, y + 6, x + 16, y + 4);
      fl_line(x + 16, y + 8, x + 18, y + 6);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kFile: {
      const int p[] = {x + 5, y + 2,  x + 12, y + 2,  x + 16, y + 6,  x + 16, y + 18,
                       x + 5, y + 18};
      stroke_path(p, 5, color);
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_line(x + 12, y + 2, x + 12, y + 6);
      fl_line(x + 12, y + 6, x + 16, y + 6);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kSettings: {
      // 简化：两条横线加端点圆，视觉上与设计稿的滑块接近
      const int p[] = {x + 4, y + 7, x + 16, y + 7, x + 4, y + 13, x + 16, y + 13};
      stroke_path(p, 4, color);
      circle(x + 8, y + 7, 2, color);
      circle(x + 13, y + 13, 2, color);
      break;
    }
    case Icon::kImport: {
      // 文件 + 向下箭头
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_rect(x + 4, y + 2, x + 9, y + 11);
      fl_line(x + 15, y + 8, x + 15, y + 17);
      fl_line(x + 12, y + 14, x + 15, y + 17);
      fl_line(x + 15, y + 17, x + 18, y + 14);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kExport: {
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_rect(x + 3, y + 9, x + 9, y + 9);
      fl_line(x + 13, y + 12, x + 13, y + 3);
      fl_line(x + 10, y + 6, x + 13, y + 3);
      fl_line(x + 13, y + 3, x + 16, y + 6);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kPlus: {
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_line(cx - 5, cy, cx + 5, cy);
      fl_line(cx, cy - 5, cx, cy + 5);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kLock: {
      // 锁体 + 上方 U 形 shackle
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_rect(x + 4, y + 9, x + 17, y + 18);
      fl_arc(x + 10, y + 9, 4, 0.0f, 3.14159265f);
      fl_line(x + 6, y + 9, x + 6, y + 13);
      fl_line(x + 15, y + 9, x + 15, y + 13);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kEye:
    case Icon::kEyeOff: {
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_arc(cx, cy, 8, 0.0f, 3.14159265f);
      fl_line(x + 2, cy, x + 18, cy);
      fl_arc(cx, cy, 8, 3.14159265f, 2 * 3.14159265f);
      circle(cx, cy, 2, color);
      if (icon == Icon::kEyeOff) {
        fl_line(x + 3, y + 3, x + 17, y + 17);
      }
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kCheck: {
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_line(x + 4, y + 11, x + 9, y + 16);
      fl_line(x + 9, y + 16, x + 17, y + 5);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kCross: {
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_line(x + 5, y + 5, x + 16, y + 16);
      fl_line(x + 16, y + 5, x + 5, y + 16);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kSearch: {
      circle(x + 9, y + 9, 5, color);
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_line(x + 13, y + 13, x + 17, y + 17);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kFolder: {
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_line(x + 3, y + 18, x + 3, y + 5);
      fl_line(x + 3, y + 5, x + 9, y + 5);
      fl_line(x + 9, y + 5, x + 11, y + 8);
      fl_line(x + 11, y + 8, x + 17, y + 8);
      fl_line(x + 17, y + 8, x + 17, y + 18);
      fl_line(x + 17, y + 18, x + 3, y + 18);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kWarning: {
      // 圆 + 感叹号
      circle(cx, cy, 8, color);
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_line(cx, cy - 4, cx, cy + 1);
      fl_line(cx, cy + 3, cx, cy + 4);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kInfo: {
      circle(cx, cy, 8, color);
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_line(cx, cy - 1, cx, cy + 4);
      fl_line(cx, cy - 4, cx, cy - 3);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kTrash: {
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_line(x + 4, y + 6, x + 16, y + 6);
      fl_line(x + 8, y + 6, x + 8, y + 3);
      fl_line(x + 8, y + 3, x + 13, y + 3);
      fl_line(x + 13, y + 3, x + 13, y + 6);
      fl_line(x + 6, y + 6, x + 6, y + 18);
      fl_line(x + 6, y + 18, x + 15, y + 18);
      fl_line(x + 15, y + 18, x + 15, y + 6);
      fl_line_style(FL_SOLID, 0);
      break;
    }
    case Icon::kCopy: {
      fl_color(color);
      fl_line_style(FL_SOLID, 1);
      fl_rect(x + 7, y + 3, x + 15, y + 14);
      fl_rect(x + 4, y + 7, x + 12, y + 18);
      fl_line_style(FL_SOLID, 0);
      break;
    }
  }
}

}  // namespace secretkeeper::ui::widgets
