#pragma once

// SecretKeeper - 界面主题（视觉常量与绘制助手）v2
//
// 取值全部来自 docs/ui/UI设计规范-v2.md（重设计规范），三端共用一份。
// v2 要点：三层背景分层替代描边围栏、主色加深、字号/间距 token 化、
// 全部可交互件具备 hover/focus/禁用状态。v1 常量名保留（页面层引用
// 不必全改），值按 v2 更新；新增 token 一律用 v2 命名。

#include <FL/Enumerations.H>
#include <FL/Fl_Widget.H>
#include <string>
#include <string_view>

namespace secretkeeper::ui::theme {

// ---- 色彩（docs/ui/UI设计规范.md 第 1 节）----
inline constexpr Fl_Color kSidebarBg = 0x1A3B5D00;   // 导航栏 navy（v3 Bitwarden #1A3B5D）
inline constexpr Fl_Color kSidebarIcon = 0x9FB6D000;  // 导航未选中图标（v3 #9FB6D0）
inline constexpr Fl_Color kSidebarActiveBg = 0x152F4A00;  // 导航选中底（v3 #152F4A）
inline constexpr Fl_Color kActiveBg = 0x24454A00;  // 深色区上的浅色块（Logo 底、菜单选中）
inline constexpr Fl_Color kSidebarActiveText = 0x68C3AD00;  // 侧栏选中项文字

// 深色区专用（解锁页品牌区 #172C31 比侧栏底色更深，含配套描边与亮青）。
inline constexpr Fl_Color kBrandBg = 0x1A3B5D00;  // 解锁品牌区 navy（v3）
inline constexpr Fl_Color kBrandTeal = 0x74CFBA00;
inline constexpr Fl_Color kBrandBorder = 0x35565B00;
inline constexpr Fl_Color kCoreStroke = 0x477A7900;

// 侧栏内的描边（容量卡 / 锁定按钮）与进度条。
inline constexpr Fl_Color kSidebarBorder = 0x35505700;
inline constexpr Fl_Color kProgressTrack = 0x35505700;

// 次要按钮（设计稿的「导入主密钥」「删除主密钥」等安静样式）。
inline constexpr Fl_Color kSecondaryBg = 0xE8EDEE00;
inline constexpr Fl_Color kSecondaryText = 0x9CA9AE00;

inline constexpr Fl_Color kContentBg = 0xF5F7F900;  // 内容区底（v2.1 蓝灰 #F5F7F9）
inline constexpr Fl_Color kCardBg = 0xFFFFFF00;      // 卡片
inline constexpr Fl_Color kFieldBg = 0xFFFFFF00;      // 输入框
inline constexpr Fl_Color kSubtleBg = 0xF8FAFA00;    // 卡片内的浅底块

inline constexpr Fl_Color kAccent = 0x175DDC00;    // 主色 Bitwarden 蓝（v3 #175DDC）
inline constexpr Fl_Color kAccentSoft = 0xE3F1ED00;  // 主色浅底（v2）
inline constexpr Fl_Color kAccentHover = 0x134EAF00;  // 主色 hover（v3 #134EAF）

inline constexpr Fl_Color kText = 0x1A262B00;      // 主文字（v2 #1A262B）
inline constexpr Fl_Color kTextMuted = 0x5F707800;   // 次要文字（v2 #5F7078）
inline constexpr Fl_Color kTextFaint = 0x9AA9AE00;    // 占位符 / 极弱文字（v2）

inline constexpr Fl_Color kBorder = 0xE9EDEF00;   // 分隔线 hairline（v2）
inline constexpr Fl_Color kBorderSubtle = 0xE8EDEE00;  // 卡片内分隔线

inline constexpr Fl_Color kWarningBg = 0xFFF7E300;    // 黄色提示条底
inline constexpr Fl_Color kWarningBorder = 0xB5831800;  // 黄色提示条描边
inline constexpr Fl_Color kWarningText = 0xB5831800;

inline constexpr Fl_Color kDangerBg = 0xFCF0EF00;    // 红色提示条底
inline constexpr Fl_Color kDangerBorder = 0xB2474700;
inline constexpr Fl_Color kDangerText = 0xB2474700;

inline constexpr Fl_Color kOnAccent = 0xFFFFFF00;  // 主色按钮上的文字

// ---- v2 新增（UI设计规范-v2.md §2.1）----
inline constexpr Fl_Color kSurfaceRaised = 0xF8FAFA00;  // 卡内嵌套区（表头/搜索框底）
inline constexpr Fl_Color kSidebarHover = 0x1C3B5C00;   // 导航项 hover（v3）
inline constexpr Fl_Color kSidebarAccentBar = 0x175DDC00;  // 导航/列表选中左缘 3px 竖条（v3 主蓝）
inline constexpr Fl_Color kInputFocus = 0x175DDC00;      // 输入框聚焦描边（v3 主蓝）
inline constexpr Fl_Color kInputBorder = 0xCBD5D900;     // 输入框常规描边
inline constexpr Fl_Color kDanger = 0xB23B3B00;          // 危险动作（v2 收敛）
inline constexpr Fl_Color kDangerSoft = 0xFBEFEE00;      // 错误条底
inline constexpr Fl_Color kWarning = 0xA9791C00;         // 警示文字（v2）
inline constexpr Fl_Color kWarningSoft = 0xFFF6E000;     // 警示条底
inline constexpr Fl_Color kOk = 0x2E7D5B00;              // 成功/已验证
inline constexpr Fl_Color kRowHover = 0xF8FAFA00;        // 列表行 hover
inline constexpr Fl_Color kCardShadow = 0xE3E8EA00;      // 卡片底部 2px 模拟阴影
inline constexpr Fl_Color kCategoryIconBg = 0xE7EEF200;  // v2.1 行首分类图标圆底
inline constexpr Fl_Color kCategoryIconFg = 0x3B6EA500;  // v2.1 分类图标色（Bitwarden 中蓝）
inline constexpr Fl_Color kTextSub = kTextMuted;         // v2 别名（次要文字）
inline constexpr Fl_Color kHairline = kBorder;           // v2 别名（分隔线）
inline constexpr Fl_Color kScrollbarThumb = 0xC4CFD300;  // 滚动条 thumb
inline constexpr Fl_Color kNavyDeep = 0x12325200;       // 解锁品牌区渐变深端（v3）
inline constexpr Fl_Color kPillBg = 0xE8EDEE00;         // 禁用控件底色
inline constexpr Fl_Color kScrollbarThumbHover = 0xAEBDC200;
inline constexpr Fl_Color kAccentBar = 0x175DDC00;       // 列表选中左缘 3px 竖条（v3 主蓝）

// ---- v2 字号阶梯（§2.4）----
inline constexpr int kFsCaption = 11;
inline constexpr int kFsSmall = 12;
inline constexpr int kFsBody = 13;      // v2 正文基准 13（v1 的 14 降半档）
inline constexpr int kFsRow = 14;       // 列表行标题（Medium）
inline constexpr int kFsCardTitle = 15; // 卡片标题（SemiBold）
inline constexpr int kFsPageTitle = 22; // 页面标题（v1 24 → 22）
inline constexpr int kFsDisplay = 28;   // 解锁页品牌语

// ---- 圆角（第 2 节）----
inline constexpr int kRadiusCard = 12;   // v2 卡片 12
inline constexpr int kRadiusField = 8;
inline constexpr int kRadiusButton = 8;  // v2 按钮 8
inline constexpr int kRadiusPill = 100;
inline constexpr int kRadiusBadge = 4;   // 徽章/小件

// ---- 字号（第 7 节）----
inline constexpr int kFontTitle = 22;    // 页面大标题（v2 22）
inline constexpr int kFontHeading = 15;   // 卡片标题（v2 kFsCardTitle）
inline constexpr int kFontSubheading = 14;  // 表单分组标题（kFsRow）
inline constexpr int kFontBody = 13;     // 正文（v2 基准 13）
inline constexpr int kFontLabel = 12;     // 字段标签（v2 kFsSmall）
inline constexpr int kFontSmall = 12;    // 说明 / 提示
inline constexpr int kFontTiny = 11;     // 计数 / 角标（kFsCaption）
inline constexpr int kFontMono = 13;     // ID/哈希等宽（FL_COURIER）

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
inline constexpr int kSidebarWidth = 64;    // 图标导航栏宽（v3 Bitwarden 式）
inline constexpr int kContentPad = 32;      // 主内容区四周留白

// 用户可调窗口的下限。size_range 的 min 以下拖不动；max 传 0（无上限）
// 才会让 FLTK 在 Windows 上给窗口加 WS_THICKFRAME 与最大化按钮。
inline constexpr int kMinWinW = 760;
inline constexpr int kMinWinH = 520;

inline constexpr int kUnlockH = 784;        // 01 解锁与锁定（820）
inline constexpr int kKeysH = 839;          // 02 主密钥管理与详情（875）
inline constexpr int kCreateKeyH = 833;     // 03 生成主密钥（869）
inline constexpr int kImportKeyH = 811;     // 04 导入主密钥（847）
inline constexpr int kExportKeyH = 905;     // 05 主密钥文件导出（941）
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
