#pragma once

// SecretKeeper - 界面主题（视觉常量与绘制助手）
//
// 取值全部来自 docs/ui/UI设计规范.md，21 张设计稿是唯一视觉真相源。
// 桌面三端共用这一份，Windows / Linux / macOS 呈现完全一致。
//
// 为什么单独一层：界面代码里散落着魔法数字时，改一次配色要翻遍所有页面。
// 这里集中之后，改主题只改本文件。

#include <FL/Enumerations.H>
#include <FL/Fl_Widget.H>
#include <string>
#include <string_view>

namespace secretkeeper::ui::theme {

// ---- 色彩（docs/ui/UI设计规范.md 第 1 节）----
inline constexpr Fl_Color kSidebarBg = 0x1D2C31;   // 侧栏底色
inline constexpr Fl_Color kSidebarIcon = 0x96AFB3;  // 侧栏未选中图标
inline constexpr Fl_Color kSidebarActiveBg = 0x24454A;  // 侧栏选中项底色
inline constexpr Fl_Color kActiveBg = 0x24454A;  // 深色区上的浅色块（Logo 底、菜单选中）
inline constexpr Fl_Color kSidebarActiveText = 0x68C3AD;  // 侧栏选中项文字

inline constexpr Fl_Color kContentBg = 0xF3F5F6;  // 主内容区底色
inline constexpr Fl_Color kCardBg = 0xFFFFFF;      // 卡片
inline constexpr Fl_Color kFieldBg = 0xFFFFFF;      // 输入框
inline constexpr Fl_Color kSubtleBg = 0xF8FAFA;    // 卡片内的浅底块

inline constexpr Fl_Color kAccent = 0x14786D;    // 主色（按钮 / 描边）
inline constexpr Fl_Color kAccentSoft = 0xE8F4F0;  // 主色浅底（提示条）
inline constexpr Fl_Color kAccentHover = 0x3D6B6D;

inline constexpr Fl_Color kText = 0x1D2C31;      // 主文字
inline constexpr Fl_Color kTextMuted = 0x718087;   // 次要文字
inline constexpr Fl_Color kTextFaint = 0x9CA9AE;    // 占位符 / 极弱文字

inline constexpr Fl_Color kBorder = 0xDFE5E7;   // 常规描边
inline constexpr Fl_Color kBorderSubtle = 0xE8EDEE;  // 卡片内分隔线

inline constexpr Fl_Color kWarningBg = 0xFFF7E3;    // 黄色提示条底
inline constexpr Fl_Color kWarningBorder = 0xB58318;  // 黄色提示条描边
inline constexpr Fl_Color kWarningText = 0xB58318;

inline constexpr Fl_Color kDangerBg = 0xFCF0EF;    // 红色提示条底
inline constexpr Fl_Color kDangerBorder = 0xB24747;
inline constexpr Fl_Color kDangerText = 0xB24747;

inline constexpr Fl_Color kOnAccent = 0xFFFFFF;  // 主色按钮上的文字

// ---- 圆角（第 2 节）----
inline constexpr int kRadiusCard = 8;
inline constexpr int kRadiusField = 8;
inline constexpr int kRadiusButton = 6;
inline constexpr int kRadiusPill = 100;

// ---- 字号（第 7 节）----
inline constexpr int kFontTitle = 24;    // 页面大标题
inline constexpr int kFontHeading = 20;   // 区块标题
inline constexpr int kFontSubheading = 14;  // 表单分组标题
inline constexpr int kFontBody = 14;     // 正文
inline constexpr int kFontLabel = 13;     // 字段标签
inline constexpr int kFontSmall = 12;    // 说明 / 提示
inline constexpr int kFontTiny = 11;     // 计数 / 角标

// ---- 字重（第 8 节）：FLTK 用字体族区分 400 与 700 ----
inline constexpr int kWeightRegular = 0;
inline constexpr int kWeightMedium = 1;
inline constexpr int kWeightBold = 2;

// ---- 间距（第 4 节）----
inline constexpr int kGapXs = 4;
inline constexpr int kGapSm = 8;
inline constexpr int kGapMd = 12;
inline constexpr int kGapLg = 16;
inline constexpr int kGapXl = 24;
inline constexpr int kGapXxl = 32;

// 侧栏宽度与顶栏高度（设计稿 1200x844 的布局实测值）。
inline constexpr int kSidebarWidth = 210;
inline constexpr int kContentPad = 24;

// 列表与详情两栏的宽度（设计稿 02 / 06 号）。
inline constexpr int kListColumnWidth = 380;
inline constexpr int kDetailColumnWidth = 440;

// ---- 绘制助手 ----

// 圆角填充矩形。半径 <= 0 时退化为直角矩形。
void fill_rounded(int x, int y, int w, int h, int radius, Fl_Color c);

// 圆角描边矩形。
void stroke_rounded(int x, int y, int w, int h, int radius, Fl_Color c, int line_width = 1);

// 胶囊标签（状态标签 / 步骤标签）：返回实际占用宽度。
// 背景为 tag_bg，文字为 tag_fg，居中绘制。
int draw_pill(int x, int y, int w, int h, const std::string& text, Fl_Color fg,
              Fl_Color bg);

// 状态标签宽度预估（供布局计算，无需真正绘制）。
int pill_width(const std::string& text, int font_size);

// 按字重取 FLTK 字体。FLTK 只有 HELVETICA / HELVETICA_BOLD 两族，
// 设计稿的 500 字重映射到常规族、700 映射到粗体族。
Fl_Font font_for(int weight);

// 统一设置常用外观：直角边框、背景色、文字色。
void style(Fl_Widget& w, Fl_Color bg, Fl_Color fg);

}  // namespace secretkeeper::ui::theme
