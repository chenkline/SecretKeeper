#include "theme.h"

#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>

namespace secretkeeper::ui::theme {
namespace {

// FLTK 1.4 只提供 15 个内置字体族，没有加载系统中文字体的公开 API
// （1.4 移除了 1.3 的 fl_font_face）。设计稿指定 Noto Sans SC / IBM Plex Mono，
// FLTK 内无法直接使用，因此这里统一映射到内置的 Helvetica 族：
// 字重 500 与 400 同族，700 用粗体族。
// 字形由宿主的 Helvetica/Arial 别名解析，中文能否显示取决于宿主字体配置。
Fl_Font font_for_weight(int weight) {
  return weight == kWeightBold ? FL_HELVETICA_BOLD : FL_HELVETICA;
}

}  // namespace

Fl_Font font_for(int weight) { return font_for_weight(weight); }

void fill_rounded(int x, int y, int w, int h, int radius, Fl_Color c) {
  if (w <= 0 || h <= 0) return;
  fl_color(c);
  if (radius <= 0) {
    fl_rectf(x, y, w, h);
    return;
  }
  fl_rounded_rectf(x, y, w, h, radius);
}

void stroke_rounded(int x, int y, int w, int h, int radius, Fl_Color c, int line_width) {
  if (w <= 0 || h <= 0) return;
  fl_color(c);
  fl_line_style(FL_SOLID, line_width);
  if (radius <= 0) {
    fl_rect(x, y, w, h);
  } else {
    fl_rounded_rect(x, y, w, h, radius);
  }
  fl_line_style(FL_SOLID, 0);
}

int pill_width(std::string_view text, int font_size) {
  fl_font(font_for(kWeightMedium), font_size);
  return static_cast<int>(fl_width(text.c_str())) + 2 * kGapMd + kGapSm;
}

int draw_pill(int x, int y, int w, int h, std::string_view text, Fl_Color fg, Fl_Color bg) {
  fill_rounded(x, y, w, h, h / 2, bg);
  fl_color(fg);
  fl_font(font_for(kWeightMedium), kFontTiny);
  fl_draw(text.c_str(), x, y, w, h, FL_ALIGN_CENTER);
  return w;
}

void style(Fl_Widget& w, Fl_Color bg, Fl_Color fg) {
  w.box(FL_FLAT_BOX);
  w.color(bg);
  w.labelcolor(fg);
  w.labelfont(font_for(kWeightRegular));
  w.labelsize(kFontBody);
}

void draw_text(Fl_Widget& w, std::string_view text, int x, int y, int width, int height,
               Fl_Align align) {
  // 借用 Fl_Box 的绘制路径统一处理对齐与垂直居中。
  Fl_Box scratch(x, y, width, height, text.data());
  scratch.align(align | FL_ALIGN_INSIDE);
  scratch.box(FL_NO_BOX);
  scratch.labelfont(font_for(kWeightRegular));
  scratch.labelsize(kFontBody);
  w.draw_label(scratch);
}

}  // namespace secretkeeper::ui::theme
