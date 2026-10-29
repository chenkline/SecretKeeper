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
inline constexpr Fl_Color kSidebarBg = 0x1D2C3100;   // 侧栏底色
inline constexpr Fl_Color kSidebarIcon = 0x96AFB300;  // 侧栏未选中图标
inline constexpr Fl_Color kSidebarActiveBg = 0x24454A00;  // 侧栏选中项底色
inline constexpr Fl_Color kActiveBg = 0x24454A00;  // 深色区上的浅色块（Logo 底、菜单选中）
inline constexpr Fl_Color kSidebarActiveText = 0x68C3AD00;  // 侧栏选中项文字

// 深色区专用（解锁页品牌区 #172C31 比侧栏底色更深，含配套描边与亮青）。
inline constexpr Fl_Color kBrandBg = 0x172C3100;
inline constexpr Fl_Color kBrandTeal = 0x74CFBA00;
inline constexpr Fl_Color kBrandBorder = 0x35565B00;
inline constexpr Fl_Color kCoreStroke = 0x477A7900;

// 侧栏内的描边（容量卡 / 锁定按钮）与进度条。
inline constexpr Fl_Color kSidebarBorder = 0x35505700;
inline constexpr Fl_Color kProgressTrack = 0x35505700;

// 次要按钮（设计稿的「导入主密钥」「删除主密钥」等安静样式）。
inline constexpr Fl_Color kSecondaryBg = 0xE8EDEE00;
inline constexpr Fl_Color kSecondaryText = 0x9CA9AE00;

inline constexpr Fl_Color kContentBg = 0xF3F5F600;  // 主内容区底色
inline constexpr Fl_Color kCardBg = 0xFFFFFF00;      // 卡片
inline constexpr Fl_Color kFieldBg = 0xFFFFFF00;      // 输入框
inline constexpr Fl_Color kSubtleBg = 0xF8FAFA00;    // 卡片内的浅底块

inline constexpr Fl_Color kAccent = 0x14786D00;    // 主色（按钮 / 描边）
inline constexpr Fl_Color kAccentSoft = 0xE8F4F000;  // 主色浅底（提示条）
inline constexpr Fl_Color kAccentHover = 0x3D6B6D00;

inline constexpr Fl_Color kText = 0x1D2C3100;      // 主文字
inline constexpr Fl_Color kTextMuted = 0x71808700;   // 次要文字
inline constexpr Fl_Color kTextFaint = 0x9CA9AE00;    // 占位符 / 极弱文字

inline constexpr Fl_Color kBorder = 0xDFE5E700;   // 常规描边
inline constexpr Fl_Color kBorderSubtle = 0xE8EDEE00;  // 卡片内分隔线

inline constexpr Fl_Color kWarningBg = 0xFFF7E300;    // 黄色提示条底
inline constexpr Fl_Color kWarningBorder = 0xB5831800;  // 黄色提示条描边
inline constexpr Fl_Color kWarningText = 0xB5831800;

inline constexpr Fl_Color kDangerBg = 0xFCF0EF00;    // 红色提示条底
inline constexpr Fl_Color kDangerBorder = 0xB2474700;
inline constexpr Fl_Color kDangerText = 0xB2474700;

inline constexpr Fl_Color kOnAccent = 0xFFFFFF00;  // 主色按钮上的文字

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

// ---- 页面几何（docs/ui/svg/ 桌面画板实测值）----
//
// 设计稿宽 1200、高 820~894，其中顶部 36px 是自绘窗口标题栏；
// 实现用系统标题栏，客户区高 = 画板高 − 36。窗口高度随页面切换。

inline constexpr int kWinW = 1200;          // 全部桌面画板统一宽度
inline constexpr int kSidebarWidth = 208;   // 侧栏宽（02-09 号画板公共）
inline constexpr int kContentPad = 32;      // 主内容区四周留白

inline constexpr int kUnlockH = 784;        // 01 解锁与锁定（820）
inline constexpr int kKeysH = 839;          // 02 主密钥管理与详情（875）
inline constexpr int kCreateKeyH = 833;     // 03 生成主密钥（869）
inline constexpr int kImportKeyH = 811;     // 04 导入主密钥（847）
inline constexpr int kExportKeyH = 787;     // 05 主密钥文件导出（823）
inline constexpr int kSecretsH = 858;       // 06 机密信息列表与详情（894）
inline constexpr int kAddSecretH = 784;     // 07 新增机密信息（820）
inline constexpr int kImportSecretH = 817;  // 08 导入机密信息与密码验证（853）
inline constexpr int kSettingsH = 784;      // 09 安全设置（820）

// 列表与详情两栏的宽度（设计稿 02 / 06 号）。
inline constexpr int kListColumnWidth = 384;
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
