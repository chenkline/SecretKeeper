// SecretKeeper - 页面层实现
//
// 布局取值全部来自 docs/ui/svg/ 的九张桌面端设计稿；业务判断在
// view_model.cpp，密钥操作在 core/service.h，本文件不接触密钥材料。
//
// 密码交互沿用既有约定：先带空密码调用 service，只有 service 回
// kNeedsPassword 才弹框索要密码并重试同一个调用。

#include "pages.h"

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <array>
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "main_window.h"
#include "platform.h"
#include "view_model.h"

#include "core/service.h"

namespace secretkeeper::ui {
namespace fs = std::filesystem;
namespace svc = secretkeeper::service;

namespace {

using widgets::ButtonKind;
using widgets::Icon;

// 界面固定尺寸：宽 1200（theme::kWinW）；高度按页不同，见 theme.h 的
// kUnlockH 等常量。尚未重排的页面暂用 kWinH 兜底，重排时逐页替换。
constexpr int kWinW = theme::kWinW;
constexpr int kWinH = theme::kKeysH;

std::span<const std::uint8_t> as_bytes(const std::string& text) {
  return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

// 密码用完立刻清零，界面不得保留明文。
void scrub(std::string* text) {
  if (text == nullptr) return;
  std::fill(text->begin(), text->end(), '\0');
  text->clear();
}

bool write_bytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return out.good();
}

std::optional<std::vector<std::uint8_t>> read_bytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
  if (in.bad()) return std::nullopt;
  return bytes;
}

// 设计稿把说明性文案放在浅色圆角块里，这里统一用它。
widgets::Notice* make_notice(int x, int y, int w, widgets::Notice::Tone tone,
                             std::string_view title, std::string_view body, int height) {
  auto* notice = new widgets::Notice(x, y, w, height, tone);
  notice->set_content(title, body);
  return notice;
}

// Unix 毫秒时间戳 → "2026年09月20日"（02/06 号画板卡片元信息用）。
std::string format_date(std::int64_t ms) {
  if (ms <= 0) return "";
  const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d年%02d月%02d日", local.tm_year + 1900,
                local.tm_mon + 1, local.tm_mday);
  return buf;
}

// 「标签 : 值」形式的信息行。设计稿的详情区全是这种两列排布。
void draw_label_value(Fl_Widget& target, int x, int y, int label_w, int value_w,
                      std::string_view label, std::string_view value, bool value_bold) {
  fl_color(theme::kTextMuted);
  fl_font(theme::font_for(theme::kWeightRegular), theme::kFontLabel);
  fl_draw(label.data(), x, y, label_w, 24, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  fl_color(theme::kText);
  fl_font(theme::font_for(value_bold ? theme::kWeightBold : theme::kWeightRegular),
          theme::kFontBody);
  fl_draw(value.data(), x + label_w, y, value_w, 24, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
}

void draw_divider(Fl_Widget& target, int x, int y, int w) {
  fl_color(theme::kBorderSubtle);
  fl_rect(x, y, w, 1);
}

// ---- 密码输入框 ----
// 需求要求密码框掩码显示且不自动纠错大写；FLTK 的 FL_SECRET_INPUT 即掩码。
struct PasswordPrompt {
  bool accepted = false;
  std::string first;
  std::string second;
};

// FLTK 回调是 C 风格函数指针，无法捕获局部变量，故用上下文结构传递。
struct PasswordDialog {
  Fl_Window* window = nullptr;
  widgets::Field* first = nullptr;
  widgets::Field* second = nullptr;
  PasswordPrompt* out = nullptr;
  bool confirm = false;
};

void password_accept_cb(Fl_Widget*, void* data) {
  auto* d = static_cast<PasswordDialog*>(data);
  const char* a = d->first->value();
  if (a == nullptr || a[0] == '\0') {
    fl_message("%s", "密码不能为空");
    return;
  }
  if (d->confirm && std::strcmp(a, d->second->value()) != 0) {
    fl_message("%s", "两次输入的密码不一致");
    return;
  }
  d->out->accepted = true;
  d->window->hide();
}

void password_cancel_cb(Fl_Widget* w, void* data) {
  static_cast<PasswordDialog*>(data)->window->hide();
  (void)w;
}

void ask_password(const char* title, const char* body, const char* first_label,
                  const char* second_label, PasswordPrompt* out) {
  const bool confirm = second_label != nullptr;
  const int h = confirm ? 268 : 212;
  Fl_Window win(460, h, title);
  win.color(theme::kContentBg);
  win.begin();

  new widgets::Card(16, 16, 428, h - 60);

  Fl_Box head(36, 34, 388, 26, title);
  head.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  head.labelfont(theme::font_for(theme::kWeightBold));
  head.labelsize(theme::kFontHeading);
  head.labelcolor(theme::kText);

  Fl_Box hint(36, 62, 388, 42, body);
  hint.align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  hint.labelfont(theme::font_for(theme::kWeightRegular));
  hint.labelsize(theme::kFontSmall);
  hint.labelcolor(theme::kTextMuted);

  PasswordDialog d;
  d.window = &win;
  d.out = out;
  d.confirm = confirm;

  Fl_Box flabel1(36, 106, 388, 18, first_label);
  flabel1.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  flabel1.labelfont(theme::font_for(theme::kWeightRegular));
  flabel1.labelsize(theme::kFontSmall);
  flabel1.labelcolor(theme::kTextMuted);
  d.first = new widgets::Field(36, 126, 388, 38);
  d.first->type(FL_SECRET_INPUT);
  d.first->when(FL_WHEN_ENTER_KEY);
  d.first->callback(password_accept_cb, &d);

  if (confirm) {
    Fl_Box flabel2(36, 172, 388, 18, second_label);
    flabel2.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    flabel2.labelfont(theme::font_for(theme::kWeightRegular));
    flabel2.labelsize(theme::kFontSmall);
    flabel2.labelcolor(theme::kTextMuted);
    d.second = new widgets::Field(36, 192, 388, 38);
    d.second->type(FL_SECRET_INPUT);
    d.second->when(FL_WHEN_ENTER_KEY);
    d.second->callback(password_accept_cb, &d);
  }

  auto* ok = new widgets::IconButton(244, h - 54, 96, 36, Icon::kLock, "确定");
  auto* cancel = new widgets::Button(136, h - 54, 96, 36, "取消", ButtonKind::kSecondary);
  win.end();
  ok->callback(password_accept_cb, &d);
  cancel->callback(password_cancel_cb, &d);

  d.first->take_focus();
  win.show();
  while (win.shown()) Fl::wait(0.05);

  if (out->accepted) {
    out->first = d.first->value() ? d.first->value() : "";
    if (confirm && d.second != nullptr) out->second = d.second->value() ? d.second->value() : "";
  }
  // 立即清空控件里的明文，再清 out 中的副本。
  d.first->value("");
  if (d.second != nullptr) d.second->value("");
  scrub(&out->first);
  scrub(&out->second);
}

}  // namespace

// =====================================================================
// PageBase
// =====================================================================
PageBase::PageBase(MainWindow* host, const char* title, const char* subtitle)
    : Fl_Group(0, 0, kWinW, kWinH), host_(host),
      subtitle_(subtitle ? subtitle : "") {
  box(FL_NO_BOX);
  color(theme::kContentBg);
  back_ = nullptr;
  // 页头（02/03 号画板）：内容区 x=208+32=240，标题 fs24、说明 fs13。
  title_box_ = new Fl_Box(theme::kSidebarWidth + theme::kContentPad, 32, 656, 33, title);
  title_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  title_box_->labelfont(theme::font_for(theme::kWeightRegular));
  title_box_->labelsize(24);
  title_box_->labelcolor(theme::kText);
  subtitle_box_ = new Fl_Box(theme::kSidebarWidth + theme::kContentPad, 72, 700, 21,
                             subtitle_.c_str());
  subtitle_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  subtitle_box_->labelfont(theme::font_for(theme::kWeightRegular));
  subtitle_box_->labelsize(theme::kFontLabel);
  subtitle_box_->labelcolor(theme::kTextMuted);
  end();
}

void PageBase::draw() {
  // 先画 02-09 号画板公共的侧栏装饰，再画本页子控件。
  host_->paint_sidebar_decor();
  Fl_Group::draw();
}

void PageBase::set_subtitle(const std::string& text) {
  if (subtitle_box_ == nullptr) return;
  subtitle_box_->copy_label(text.c_str());
  subtitle_box_->redraw();
}

void PageBase::layout_header(const char* back_label) {
  if (back_label != nullptr) {
    // 返回按钮（03/04/05 号画板）：白底描边，右缘与内容区对齐，高 44；
    // 宽度随文案自适应（03 号「返回主密钥管理」134px，04/05 号「返回管理」92px）。
    fl_font(theme::font_for(theme::kWeightRegular), theme::kFontBody);
    const int tw = static_cast<int>(fl_width(back_label));
    const int bw = std::max(92, tw + 36);
    back_ = new widgets::Button(0, 40, bw, 44, back_label, ButtonKind::kSecondary);
    back_->position(kWinW - theme::kContentPad - bw, 40);
    back_->callback([](Fl_Widget*, void* data) {
      static_cast<PageBase*>(data)->on_back();
    }, this);
  }
}

void PageBase::on_back() { host_->go_back(); }

// =====================================================================
// 01 解锁与锁定
// =====================================================================
UnlockPage::UnlockPage(MainWindow* host) : Fl_Group(0, 0, kWinW, theme::kUnlockH), host_(host) {
  begin();
  box(FL_NO_BOX);
  color(theme::kBrandBg);

  // ---- 解锁区（01 号画板最终稿 x=568 w=520）----
  const int form_x = 568;
  const int form_w = 520;

  state_pill_ = new widgets::Pill(form_x, 64, 49, 21, "已锁定", theme::kTextMuted,
                                  theme::kSubtleBg);

  auto* welcome = new Fl_Box(form_x, 337, form_w, 44, "欢迎回来");
  welcome->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  welcome->labelfont(theme::font_for(theme::kWeightRegular));
  welcome->labelsize(32);
  welcome->labelcolor(theme::kText);

  auto* tip = new Fl_Box(form_x, 390, form_w, 22, "选择主密钥，输入主密钥密码解锁。");
  tip->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  tip->labelfont(theme::font_for(theme::kWeightRegular));
  tip->labelsize(theme::kFontBody);
  tip->labelcolor(theme::kTextMuted);

  auto* key_label = new Fl_Box(form_x, 435, form_w, 17, "主密钥");
  key_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  key_label->labelfont(theme::font_for(theme::kWeightRegular));
  key_label->labelsize(theme::kFontLabel);
  key_label->labelcolor(theme::kText);

  // 主密钥卡（01 号最终稿：白底主色描边，h=65）。
  selector_ = new widgets::KeySelector(form_x, 458, form_w, 65);
  selector_->set_placeholder("尚未创建主密钥，请先生成");
  selector_->callback([](Fl_Widget*, void* data) {
    static_cast<UnlockPage*>(data)->on_pick_key();
  }, this);

  auto* pwd_label = new Fl_Box(form_x, 553, form_w, 17, "主密钥密码");
  pwd_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  pwd_label->labelfont(theme::font_for(theme::kWeightRegular));
  pwd_label->labelsize(theme::kFontLabel);
  pwd_label->labelcolor(theme::kText);

  password_ = new widgets::Field(form_x, 576, form_w, 46);
  password_->type(FL_SECRET_INPUT);
  password_->when(FL_WHEN_ENTER_KEY);
  password_->callback([](Fl_Widget*, void* data) {
    static_cast<UnlockPage*>(data)->on_unlock();
  }, this);

  unlock_btn_ = new widgets::IconButton(form_x, 646, form_w, 44, Icon::kLock, "解锁密匣");
  unlock_btn_->callback([](Fl_Widget*, void* data) {
    static_cast<UnlockPage*>(data)->on_unlock();
  }, this);

  auto* hint = new Fl_Box(form_x, 720, form_w, 19,
                          "密码丢失无法解密；密匣没有在线找回方式。");
  hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  hint->labelfont(theme::font_for(theme::kWeightRegular));
  hint->labelsize(theme::kFontSmall);
  hint->labelcolor(theme::kTextMuted);

  // 错误提示：设计稿无固定位置，放在提示行下方。
  error_ = new Fl_Box(form_x, 744, form_w, 19, "");
  error_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  error_->labelfont(theme::font_for(theme::kWeightRegular));
  error_->labelsize(theme::kFontSmall);
  error_->labelcolor(theme::kTextMuted);
  end();
}

void UnlockPage::draw() {
  // 01 号画板：右解锁区底色为纯白（其余内容页是 #F3F5F6），先铺底再画子控件。
  fl_color(theme::kFieldBg);
  fl_rectf(0, 0, w(), h());
  Fl_Group::draw();

  // ---- 左侧品牌区（01 号画板 x=0..456，#172C31）----
  const int side_w = 456;
  fl_color(theme::kBrandBg);
  fl_rectf(0, 0, side_w, h());

  // 品牌行：盾牌 30px + 密匣 fs26 白 + SecretKeeper fs12。
  widgets::draw_icon(Icon::kKey, 48, 84, theme::kBrandTeal);
  fl_color(0xFFFFFF00);
  fl_font(theme::font_for(theme::kWeightRegular), 26);
  fl_draw("密匣", 94, 88, 120, 32, FL_ALIGN_LEFT);
  fl_color(theme::kSidebarIcon);
  fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
  fl_draw("SecretKeeper", 94, 119, 140, 15, FL_ALIGN_LEFT);

  // 安全视觉：两个同心圆 + 安全核心 + 本机加密角标（01 号画板实测）。
  // fl_arc 的角度单位是度，不是弧度。
  fl_line_style(FL_SOLID, 1);
  fl_color(theme::kBrandBorder);
  fl_arc(188, 316, 122, 0, 360);
  fl_color(theme::kAccentHover);
  fl_arc(188, 317, 91, 0, 360);
  fl_line_style(FL_SOLID, 0);
  theme::fill_rounded(138, 260, 100, 114, 20, theme::kActiveBg);
  theme::stroke_rounded(138, 260, 100, 114, 20, theme::kCoreStroke);
  widgets::draw_icon(Icon::kCheck, 178, 306, theme::kBrandTeal);

  theme::fill_rounded(225, 380, 56, 28, 6, theme::kBrandBg);
  theme::stroke_rounded(225, 380, 56, 28, 6, theme::kBrandBorder);
  fl_color(theme::kBrandTeal);
  fl_font(theme::font_for(theme::kWeightRegular), 10);
  fl_draw("本机加密", 225, 380, 56, 28, FL_ALIGN_CENTER);

  // 版本号（01 号画板最终稿：只留 v0.0.1）。
  fl_color(theme::kSidebarIcon);
  fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
  fl_draw("v0.0.1", 48, 724, 120, 15, FL_ALIGN_LEFT);
}

int UnlockPage::handle(int event) {
  if (event == FL_PUSH || event == FL_KEYDOWN || event == FL_MOUSEWHEEL) {
    host_->note_activity();
  }
  return Fl_Group::handle(event);
}

void UnlockPage::reload() {
  const auto keys = host_->service().list_master_keys();
  const std::optional<std::string> def = host_->service().default_master_key_id();

  if (keys.empty()) {
    selector_->set_placeholder("尚未创建主密钥，请先生成");
    selector_->set_key("", "", false);
    selected_key_id_.clear();
    unlock_btn_->set_enabled(false);
    error_->copy_label("请先在主密钥管理中生成一把主密钥。");
    error_->labelcolor(theme::kTextMuted);
    return;
  }

  unlock_btn_->set_enabled(true);
  // 默认选中：优先当前默认主密钥，否则第一把。
  int index = 0;
  if (def.has_value()) {
    for (std::size_t i = 0; i < keys.size(); ++i) {
      if (keys[i].master_key_id == *def) {
        index = static_cast<int>(i);
        break;
      }
    }
  }
  const auto& item = keys[static_cast<std::size_t>(index)];
  selected_key_id_ = item.master_key_id;
  selector_->set_key(item.name, item.master_key_id, item.is_default);
  selector_->set_switchable(keys.size() > 1);
  error_->copy_label("");
}

void UnlockPage::on_pick_key() {
  const auto keys = host_->service().list_master_keys();
  if (keys.size() <= 1) {
    // 只有一把时无需选择，但仍要保证选中态正确。
    if (!keys.empty()) {
      selected_key_id_ = keys[0].master_key_id;
      selector_->set_key(keys[0].name, keys[0].master_key_id, keys[0].is_default);
    }
    return;
  }
  // 轮询选择：每次点击切到下一把，密钥最多两把，循环切换即可，
  // 避免引入下拉浮层带来的层级与焦点复杂度。
  int index = 0;
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (keys[i].master_key_id == selected_key_id_) {
      index = static_cast<int>(i);
      break;
    }
  }
  index = (index + 1) % static_cast<int>(keys.size());
  const auto& item = keys[static_cast<std::size_t>(index)];
  selected_key_id_ = item.master_key_id;
  selector_->set_key(item.name, item.master_key_id, item.is_default);
  host_->note_activity();
}

std::string UnlockPage::status_text() const {
  return unlocked_ ? "已解锁" : "已锁定";
}

void UnlockPage::on_unlock() {
  if (selected_key_id_.empty()) {
    error_->copy_label("请先选择主密钥");
    error_->labelcolor(theme::kDangerText);
    return;
  }
  const svc::BackoffStatus st = host_->service().backoff();
  if (st.waiting) {
    error_->copy_label(backoff_text(st).c_str());
    error_->labelcolor(theme::kWarningText);
    return;
  }
  const std::string pwd = password_->value() ? password_->value() : "";
  if (pwd.empty()) {
    error_->copy_label("请输入主密钥密码");
    error_->labelcolor(theme::kDangerText);
    return;
  }
  std::string password = pwd;
  password_->value("");
  const svc::Error rc = host_->service().unlock(selected_key_id_, as_bytes(password));
  scrub(&password);

  if (rc != svc::Error::kOk) {
    error_->copy_label(error_text(rc).c_str());
    error_->labelcolor(theme::kDangerText);
    return;
  }
  error_->copy_label("");
  unlocked_ = true;
  host_->on_unlocked();
}


// =====================================================================
// 02 主密钥管理
// =====================================================================
MasterKeysPage::MasterKeysPage(MainWindow* host)
    : PageBase(host, "主密钥管理", "主密钥用于加密信息；密码仅用于解开本机保存的主密钥。") {
  begin();
  layout_header(nullptr);

  // 页头右侧入口（02 号画板）：安静样式，右缘与内容区对齐。
  gen_btn_ = new widgets::IconButton(kWinW - theme::kContentPad - 132, 40, 132, 44,
                                     Icon::kPlus, "生成主密钥", ButtonKind::kQuiet);
  imp_btn_ = new widgets::IconButton(kWinW - theme::kContentPad - 132 - 10 - 106, 40, 106, 44,
                                     Icon::kImport, "导入主密钥", ButtonKind::kQuiet);
  imp_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<MasterKeysPage*>(d)->on_import();
  }, this);
  gen_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<MasterKeysPage*>(d)->on_generate();
  }, this);

  // 容量已满提示（02 号画板 y=117，黄底 r8）。
  quota_notice_ = make_notice(theme::kSidebarWidth + theme::kContentPad, 117,
                              kWinW - theme::kSidebarWidth - theme::kContentPad * 2,
                              widgets::Notice::Tone::kWarning, "", "", 68);
  quota_notice_->set_shown(false);

  // 左列：列表标题 + 卡片（卡片在 draw() 绘制，handle() 命中检测）。
  list_label_ = new Fl_Box(theme::kSidebarWidth + theme::kContentPad, 215, 384, 19,
                           "本机主密钥　0 把");
  list_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  list_label_->labelfont(theme::font_for(theme::kWeightRegular));
  list_label_->labelsize(theme::kFontSmall);
  list_label_->labelcolor(theme::kTextMuted);
  card_box_ = new Fl_Box(theme::kSidebarWidth + theme::kContentPad, 240,
                         theme::kListColumnWidth, 422, "");
  card_box_->box(FL_NO_BOX);

  // 右列详情卡片（02 号画板最终稿 x=648 y=209 w=520 h=553，pad 24）。
  const int dx = 648;
  const int dw = 520;
  const int ix = dx + 24;
  const int iw = dw - 48;
  new widgets::Card(dx, 209, dw, 553);

  auto* dtitle = new Fl_Box(ix, 240, 300, 28, "主密钥详情");
  dtitle->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  dtitle->labelfont(theme::font_for(theme::kWeightBold));
  dtitle->labelsize(18);
  dtitle->labelcolor(theme::kText);

  default_pill_ = new widgets::Pill(ix + iw - 71, 233, 71, 21, "默认主密钥",
                                    theme::kAccent, theme::kAccentSoft);

  // 属性五行：标签 fs13 次要色（宽 86）+ 值 fs14，行距 38。
  detail_id_ = detail_name_ = detail_alg_ = detail_count_ = nullptr;
  const char* row_labels[5] = {"主密钥 ID", "名称", "算法", "保存位置", "关联信息"};
  Fl_Box** row_boxes[5] = {&detail_id_, &detail_name_, &detail_alg_, nullptr, &detail_count_};
  for (int r = 0; r < 5; ++r) {
    auto* lab = new Fl_Box(ix, 291 + r * 38, 86, 16, row_labels[r]);
    lab->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lab->labelfont(theme::font_for(theme::kWeightRegular));
    lab->labelsize(theme::kFontLabel);
    lab->labelcolor(theme::kTextMuted);
    if (row_boxes[r] != nullptr) {
      Fl_Box* box = new Fl_Box(ix + 98, 291 + r * 38, iw - 98, 22, "");
      box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
      box->labelfont(theme::font_for(theme::kWeightRegular));
      box->labelsize(theme::kFontBody);
      box->labelcolor(theme::kText);
      *row_boxes[r] = box;
    } else {
      Fl_Box* box = new Fl_Box(ix + 98, 291 + r * 38, iw - 98, 22, "本机加密存储");
      box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
      box->labelfont(theme::font_for(theme::kWeightRegular));
      box->labelsize(theme::kFontBody);
      box->labelcolor(theme::kText);
    }
  }

  auto* divider = new Fl_Box(ix, 484, iw, 1, "");
  divider->box(FL_FLAT_BOX);
  divider->color(theme::kBorder);
  divider->labelsize(1);

  auto* note = new Fl_Box(ix, 511, iw, 19,
                          "随机生成的主密钥，由主密钥密码派生的 KEK 加密后保存在本地。");
  note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  note->labelcolor(theme::kTextMuted);
  note->labelsize(theme::kFontLabel);
  note->labelfont(theme::font_for(theme::kWeightRegular));

  // 详情操作（02 号画板最终稿）：导出主密钥主色 + 删除主密钥安静样式。
  // 「设为默认」为最终稿之外补充的功能入口（需求 2.5），放第三枚。
  exp_btn_ = new widgets::IconButton(ix, 567, 132, 44, Icon::kExport, "导出主密钥");
  del_btn_ = new widgets::IconButton(ix + 144, 567, 106, 44, Icon::kTrash, "删除主密钥",
                                     ButtonKind::kQuiet);
  switch_btn_ = new widgets::IconButton(ix + 262, 567, 106, 44, Icon::kKey, "设为默认",
                                        ButtonKind::kQuiet);
  exp_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<MasterKeysPage*>(d)->on_export();
  }, this);
  del_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<MasterKeysPage*>(d)->on_delete();
  }, this);
  switch_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<MasterKeysPage*>(d)->on_switch_default();
  }, this);

  make_notice(ix, 631, iw, widgets::Notice::Tone::kSuccess, "默认主密钥禁止删除",
              "请先切换默认主密钥。切换时需分别验证原默认与新默认两把主密钥密码。", 68);
  auto* tip3 = new Fl_Box(ix, 725, iw, 19,
                          "删除主密钥前先备份，并验证其密码。删除主密钥不会删除它加密的数据。");
  tip3->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  tip3->labelcolor(theme::kTextMuted);
  tip3->labelsize(theme::kFontSmall);
  end();
}

void MasterKeysPage::draw() {
  PageBase::draw();

  // 左列主密钥卡片（02 号画板）：选中 #E8F4F0 + 主色描边，未选中白底灰描边。
  const auto keys = host()->service().list_master_keys();
  const int cx = theme::kSidebarWidth + theme::kContentPad;
  const int cw = theme::kListColumnWidth;
  int y = 240;
  for (std::size_t i = 0; i < keys.size(); ++i) {
    const auto& item = keys[i];
    const bool is_selected = static_cast<int>(i) == selected_;
    const int ch = card_height(i);
    theme::fill_rounded(cx, y, cw, ch, theme::kRadiusField,
                        is_selected ? theme::kAccentSoft : theme::kCardBg);
    theme::stroke_rounded(cx, y, cw, ch, theme::kRadiusField,
                          is_selected ? theme::kAccent : theme::kBorder);
    widgets::draw_icon(Icon::kKey, cx + 20, y + 21,
                       is_selected ? theme::kAccent : theme::kTextMuted);

    fl_color(theme::kText);
    fl_font(theme::font_for(theme::kWeightBold), theme::kFontBody);
    fl_draw(item.name.c_str(), cx + 52, y + 25, cw - 52 - 70, 22, FL_ALIGN_LEFT);
    theme::draw_pill(cx + cw - 20 - 38, y + 20, 38, 21, item.is_default ? "默认" : "备选",
                     item.is_default ? theme::kAccent : theme::kTextMuted,
                     is_selected ? theme::kAccentSoft : theme::kSubtleBg);

    fl_color(theme::kText);
    fl_font(theme::font_for(theme::kWeightMedium), theme::kFontLabel);
    fl_draw(item.master_key_id.c_str(), cx + 20, y + 57, cw - 40, 17, FL_ALIGN_LEFT);

    fl_color(theme::kTextMuted);
    fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
    fl_draw((std::to_string(item.secret_count) + " 条机密信息 · " +
             format_date(item.created_at) +
             std::string(item.is_default ? "生成" : "导入"))
                .c_str(),
            cx + 20, y + 91, cw - 40, 19, FL_ALIGN_LEFT);
    if (item.name.empty()) {
      fl_font(theme::font_for(theme::kWeightRegular), theme::kFontTiny);
      fl_draw("名称未填写，主密钥在本机可用。", cx + 20, y + 125, cw - 40, 18,
              FL_ALIGN_LEFT);
    }
    y += ch + 12;
  }
}

int MasterKeysPage::card_height(std::size_t index) const {
  const auto keys = host()->service().list_master_keys();
  if (index >= keys.size()) return 126;
  return keys[index].name.empty() ? 160 : 126;
}

void MasterKeysPage::build_key_cards() { /* 卡片在 draw 中绘制，点击由 host 转发 */ }

void MasterKeysPage::on_card_click(int index) {
  if (index < 0 || index >= static_cast<int>(ids_.size())) return;
  selected_ = index;
  reload();
}

void MasterKeysPage::reload() {
  const auto keys = host()->service().list_master_keys();
  ids_.clear();
  for (const auto& item : keys) ids_.push_back(item.master_key_id);

  if (selected_ >= static_cast<int>(ids_.size())) selected_ = ids_.empty() ? -1 : 0;
  if (selected_ < 0 && !keys.empty()) selected_ = 0;

  list_label_->copy_label(("本机主密钥　" + std::to_string(keys.size()) + " 把").c_str());

  // 容量提示条仅在满额时出现（02 号画板黄条）。
  const bool full = keys.size() >= svc::kMaxMasterKeys;
  if (full) {
    quota_notice_->set_content(
        "主密钥容量已满 · " + std::to_string(keys.size()) + " / " +
            std::to_string(svc::kMaxMasterKeys),
        "先导出并删除一把主密钥，才能生成或导入新的主密钥。");
    quota_notice_->set_shown(true);
  } else {
    quota_notice_->set_shown(false);
  }

  if (selected_ >= 0 && selected_ < static_cast<int>(keys.size())) {
    const auto& item = keys[static_cast<std::size_t>(selected_)];
    detail_id_->copy_label(item.master_key_id.c_str());
    detail_name_->copy_label(item.name.c_str());
    detail_alg_->copy_label("RSA-2048");
    detail_count_->copy_label((std::to_string(item.secret_count) + " 条").c_str());
    default_pill_->set_shown(item.is_default);
    exp_btn_->set_enabled(true);
    del_btn_->set_enabled(!item.is_default);
    switch_btn_->set_enabled(!item.is_default);
  } else {
    detail_id_->copy_label("");
    detail_name_->copy_label("");
    detail_alg_->copy_label("");
    detail_count_->copy_label("");
    default_pill_->set_shown(false);
    exp_btn_->set_enabled(false);
    del_btn_->set_enabled(false);
    switch_btn_->set_enabled(false);
  }

  gen_btn_->set_enabled(keys.size() < svc::kMaxMasterKeys);
  imp_btn_->set_enabled(keys.size() < svc::kMaxMasterKeys);
  redraw();
}

int MasterKeysPage::handle(int event) {
  // 左列卡片无子控件，命中检测在页面层完成（02 号画板）。
  if (event == FL_PUSH) {
    const auto keys = host()->service().list_master_keys();
    const int px = Fl::event_x();
    const int py = Fl::event_y();
    const int cx = theme::kSidebarWidth + theme::kContentPad;
    if (px >= cx && px <= cx + theme::kListColumnWidth) {
      int y = 240;
      for (std::size_t i = 0; i < keys.size(); ++i) {
        const int ch = card_height(i);
        if (py >= y && py < y + ch) {
          on_card_click(static_cast<int>(i));
          return 1;
        }
        y += ch + 12;
      }
    }
  }
  return Fl_Group::handle(event);
}

std::string MasterKeysPage::status_text() const {
  return "共 " + std::to_string(host()->service().quota().master_keys) + " / " +
         std::to_string(svc::kMaxMasterKeys) + " 把主密钥";
}

void MasterKeysPage::on_generate() {
  host()->navigate(Page::kCreateKey);
}

void MasterKeysPage::on_import() {
  host()->navigate(Page::kImportKey);
}

void MasterKeysPage::on_export() {
  if (selected_ < 0 || selected_ >= static_cast<int>(ids_.size())) {
    host()->set_status("请先选择主密钥");
    return;
  }
  host()->set_export_target(ids_[static_cast<std::size_t>(selected_)]);
  host()->navigate(Page::kExportKey);
}

void MasterKeysPage::on_delete() {
  if (selected_ < 0 || selected_ >= static_cast<int>(ids_.size())) return;
  const std::string id = ids_[static_cast<std::size_t>(selected_)];
  const auto& keys = host()->service().list_master_keys();
  std::string name;
  bool is_default = false;
  for (const auto& k : keys) {
    if (k.master_key_id == id) {
      name = k.name;
      is_default = k.is_default;
    }
  }
  if (is_default) {
    fl_message("%s", svc::message(svc::Error::kCannotDeleteDefault).data());
    return;
  }
  if (fl_choice("%s", "取消", "删除", nullptr) != 1) return;

  // 先试缓存的默认 KEK，不行再索要密码。
  svc::Error rc = host()->service().delete_master_key(id, {});
  if (rc == svc::Error::kNeedsPassword) {
    PasswordPrompt p;
    ask_password("删除主密钥",
                 ("主密钥 ID: " + id + "\n主密钥名称: " + (name.empty() ? "(空)" : name) +
                  "\n请输入该主密钥密码")
                     .c_str(),
                 "主密钥密码", nullptr, &p);
    if (!p.accepted) return;
    std::string password = p.first;
    rc = host()->service().delete_master_key(id, as_bytes(password));
    scrub(&password);
  }
  if (rc != svc::Error::kOk) {
    host()->show_error(rc);
    return;
  }
  selected_ = 0;
  host()->refresh();
  host()->set_status("主密钥已删除");
}

void MasterKeysPage::on_switch_default() {
  if (selected_ < 0 || selected_ >= static_cast<int>(ids_.size())) return;
  const std::string id = ids_[static_cast<std::size_t>(selected_)];
  host()->set_status("请在安全设置中切换默认主密钥");
}


// =====================================================================
// 03 生成主密钥
// =====================================================================
CreateKeyPage::CreateKeyPage(MainWindow* host)
    : PageBase(host, "生成主密钥", "为本机创建一把随机 RSA-2048 主密钥。") {
  begin();
  layout_header("返回主密钥管理");

  // 03 号画板：整块内容在一张白卡上（240,117,928,639 r12）。
  new widgets::Card(theme::kSidebarWidth + theme::kContentPad, 117, 928, 639);
  const int ix = 264;
  const int iw = 880;

  auto* section = new Fl_Box(296, 149, 400, 24, "新主密钥");
  section->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  section->labelfont(theme::font_for(theme::kWeightBold));
  section->labelsize(17);
  section->labelcolor(theme::kText);

  auto* name_label = new Fl_Box(ix, 196, 400, 17, "主密钥名称（可选）");
  name_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  name_label->labelfont(theme::font_for(theme::kWeightRegular));
  name_label->labelsize(theme::kFontLabel);
  name_label->labelcolor(theme::kText);

  name_ = new widgets::Field(ix, 219, iw, 46);
  name_->set_placeholder("可以留空");

  auto* name_note = new Fl_Box(ix, 279, iw, 16, "名称仅用于辨识，不参与加密。");
  name_note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  name_note->labelsize(theme::kFontSmall);
  name_note->labelcolor(theme::kTextMuted);

  auto* pwd_label = new Fl_Box(ix, 320, 400, 17, "主密钥密码");
  pwd_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  pwd_label->labelfont(theme::font_for(theme::kWeightRegular));
  pwd_label->labelsize(theme::kFontLabel);
  pwd_label->labelcolor(theme::kText);

  password_ = new widgets::Field(ix, 343, iw, 46);
  password_->type(FL_SECRET_INPUT);

  auto* confirm_label = new Fl_Box(ix, 417, 400, 17, "确认主密钥密码");
  confirm_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  confirm_label->labelfont(theme::font_for(theme::kWeightRegular));
  confirm_label->labelsize(theme::kFontLabel);
  confirm_label->labelcolor(theme::kText);

  confirm_ = new widgets::Field(ix, 440, iw, 46);
  confirm_->type(FL_SECRET_INPUT);

  // 黄色提醒（03 号画板 508,68）。
  warning_ = make_notice(ix, 508, iw, widgets::Notice::Tone::kWarning,
                         "请妥善记住主密钥密码",
                         "密码丢失无法解密，密匣无法为你重置密码。", 68);

  // 容量块（03 号画板 #F8FAFA 598,68）。
  widgets::Card* cap = new widgets::Card(ix, 598, iw, 68, theme::kSubtleBg);
  (void)cap;
  capacity_title_ = new Fl_Box(ix + 40, 606, 400, 21, "已使用 0 / 2 把主密钥");
  capacity_title_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  capacity_title_->labelfont(theme::font_for(theme::kWeightBold));
  capacity_title_->labelsize(theme::kFontLabel);
  capacity_title_->labelcolor(theme::kTextMuted);
  capacity_body_ = new Fl_Box(ix + 40, 631, iw - 80, 19,
                              "容量已满。先导出并删除一把主密钥，再继续生成。");
  capacity_body_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  capacity_body_->labelsize(theme::kFontSmall);
  capacity_body_->labelcolor(theme::kTextMuted);

  // 底部按钮（03 号画板 688,44）：保存主色（满容时禁用）+ 返回白底。
  save_ = new widgets::IconButton(ix, 688, 132, 44, Icon::kKey, "保存主密钥");
  save_->callback([](Fl_Widget*, void* d) {
    static_cast<CreateKeyPage*>(d)->on_save();
  }, this);
  auto* back2 = new widgets::Button(ix + 144, 688, 92, 44, "返回管理",
                                    ButtonKind::kSecondary);
  back2->callback([](Fl_Widget*, void* d) {
    static_cast<PageBase*>(static_cast<CreateKeyPage*>(d))->on_back();
  }, this);

  error_ = new Fl_Box(ix, 742, iw, 19, "");
  error_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  error_->labelsize(theme::kFontSmall);
  error_->labelcolor(theme::kDangerText);
  end();
}

void CreateKeyPage::reload() {
  const auto quota = host()->service().quota();
  const bool full = quota.master_keys >= svc::kMaxMasterKeys;
  capacity_title_->copy_label(
      ("已使用 " + std::to_string(quota.master_keys) + " / " +
       std::to_string(svc::kMaxMasterKeys) + " 把主密钥")
          .c_str());
  if (full) {
    capacity_body_->copy_label("容量已满。先导出并删除一把主密钥，再继续生成。");
  } else {
    capacity_body_->copy_label("还可继续添加；上限由基础版限额决定。");
  }
  save_->set_enabled(!full);
  error_->copy_label(last_error_.c_str());
  redraw();
}

std::string CreateKeyPage::status_text() const { return last_error_; }

void CreateKeyPage::on_save() {
  last_error_.clear();
  const std::string pwd = password_->value() ? password_->value() : "";
  const std::string cfm = confirm_->value() ? confirm_->value() : "";
  if (pwd.empty()) {
    last_error_ = "请输入主密钥密码";
    error_->copy_label(last_error_.c_str());
    password_->take_focus();
    redraw();
    return;
  }
  if (pwd != cfm) {
    last_error_ = "两次输入的密码不一致";
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  const std::string name = name_->value() ? name_->value() : "";
  std::string password = pwd;
  const svc::Error rc = host()->service().create_master_key(name, as_bytes(password));
  scrub(&password);
  password_->value("");
  confirm_->value("");
  name_->value("");

  if (rc != svc::Error::kOk) {
    last_error_ = error_text(rc);
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  host()->refresh();
  host()->set_status("主密钥已生成");
  host()->navigate(Page::kMasterKeys);
}

// =====================================================================
// 04 导入主密钥
// =====================================================================
ImportKeyPage::ImportKeyPage(MainWindow* host)
    : PageBase(host, "导入主密钥", "从之前导出的备份文件恢复主密钥，导入后作为备选保存。") {
  begin();
  layout_header(nullptr);

  // 04 号画板：白卡（240,117,928,460 r12），无头部返回钮。
  new widgets::Card(theme::kSidebarWidth + theme::kContentPad, 117, 928, 460);
  const int ix = 264;
  const int iw = 880;

  // 文件块（04 号画板 #F3F5F6 141,96 r12）。
  widgets::Card* fcard = new widgets::Card(ix, 141, iw, 96, theme::kContentBg);
  (void)fcard;
  file_name_ = new Fl_Box(ix + 16, 169, 600, 21, "尚未选择文件");
  file_name_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  file_name_->labelfont(theme::font_for(theme::kWeightRegular));
  file_name_->labelsize(theme::kFontBody);
  file_name_->labelcolor(theme::kText);
  file_sub_ = new Fl_Box(ix + 16, 197, 600, 17, "选择之前导出的 .smkexp 备份文件");
  file_sub_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  file_sub_->labelsize(theme::kFontSmall);
  file_sub_->labelcolor(theme::kTextMuted);
  choose_btn_ = new widgets::Button(ix + iw - 16 - 96, 172, 96, 34, "选择文件",
                                    ButtonKind::kSecondary);
  choose_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ImportKeyPage*>(d)->on_choose_file();
  }, this);

  auto* prot_label = new Fl_Box(ix, 261, 400, 17, "文件保护密码");
  prot_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  prot_label->labelfont(theme::font_for(theme::kWeightRegular));
  prot_label->labelsize(theme::kFontLabel);
  prot_label->labelcolor(theme::kText);

  protection_ = new widgets::Field(ix, 285, iw, 46);
  protection_->type(FL_SECRET_INPUT);

  auto* prot_note = new Fl_Box(ix, 345, iw, 16, "导出时为这份备份设定的密码。");
  prot_note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  prot_note->labelsize(theme::kFontSmall);
  prot_note->labelcolor(theme::kTextMuted);

  auto* new_label = new Fl_Box(ix, 385, 400, 17, "新的主密钥密码");
  new_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  new_label->labelfont(theme::font_for(theme::kWeightRegular));
  new_label->labelsize(theme::kFontLabel);
  new_label->labelcolor(theme::kText);

  new_password_ = new widgets::Field(ix, 409, iw, 46);
  new_password_->type(FL_SECRET_INPUT);

  auto* new_note = new Fl_Box(ix, 469, iw, 16, "为本机保存的主密钥新设密码。");
  new_note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  new_note->labelsize(theme::kFontSmall);
  new_note->labelcolor(theme::kTextMuted);

  // 按钮（04 号画板 509,44）：导入主色 + 返回白底。
  import_btn_ = new widgets::IconButton(ix, 509, 168, 44, Icon::kImport, "导入主密钥");
  import_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ImportKeyPage*>(d)->on_import();
  }, this);
  auto* back2 = new widgets::Button(ix + 180, 509, 106, 44, "返回管理",
                                    ButtonKind::kSecondary);
  back2->callback([](Fl_Widget*, void* d) {
    static_cast<PageBase*>(static_cast<ImportKeyPage*>(d))->on_back();
  }, this);

  error_ = new Fl_Box(ix, 561, iw, 19, "");
  error_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  error_->labelsize(theme::kFontSmall);
  error_->labelcolor(theme::kDangerText);
  end();
}

void ImportKeyPage::reload() {
  const bool full = host()->service().quota().master_keys >= svc::kMaxMasterKeys;
  import_btn_->set_enabled(!full && !path_.empty());
  error_->copy_label(last_error_.c_str());
  redraw();
}

std::string ImportKeyPage::status_text() const { return last_error_; }

void ImportKeyPage::on_choose_file() {
  const std::optional<std::string> picked =
      choose_open_file("导入主密钥", "*.smkexp");
  if (!picked.has_value()) return;
  path_ = *picked;
  const std::string base = fs::path(path_).filename().string();
  file_name_->copy_label(base.c_str());
  file_sub_->copy_label("本地文件 · 已选择");
  reload();
}

void ImportKeyPage::on_import() {
  last_error_.clear();
  if (path_.empty()) {
    last_error_ = "请先选择文件";
    redraw();
    return;
  }
  const std::optional<std::vector<std::uint8_t>> bytes = read_bytes(path_);
  if (!bytes.has_value()) {
    last_error_ = "文件读写失败";
    redraw();
    return;
  }
  const std::string prot = protection_->value() ? protection_->value() : "";
  const std::string npwd = new_password_->value() ? new_password_->value() : "";
  if (prot.empty()) {
    last_error_ = "请输入文件保护密码";
    redraw();
    return;
  }
  if (npwd.empty()) {
    last_error_ = "请输入新的主密钥密码";
    redraw();
    return;
  }
  std::string protection = prot;
  std::string password = npwd;
  const svc::Error rc =
      host()->service().import_master_key(*bytes, as_bytes(protection), as_bytes(password));
  scrub(&protection);
  scrub(&password);
  protection_->value("");
  new_password_->value("");

  if (rc != svc::Error::kOk) {
    last_error_ = error_text(rc);
    redraw();
    return;
  }
  host()->refresh();
  host()->set_status("主密钥已导入");
  host()->navigate(Page::kMasterKeys);
}

// =====================================================================
// 05 导出主密钥
// =====================================================================
ExportKeyPage::ExportKeyPage(MainWindow* host)
    : PageBase(host, "导出主密钥", "为导出文件新设保护密码，并将备份保存在你信任的位置。") {
  begin();
  layout_header(nullptr);

  // 05 号画板：白卡（240,117,928,829 r12），页面客户区高 905。
  new widgets::Card(theme::kSidebarWidth + theme::kContentPad, 117, 928, 829);
  const int ix = 264;
  const int iw = 880;

  // 密钥摘要块（05 号画板 #F8FAFA 141,94 r8）。
  widgets::Card* scard = new widgets::Card(ix, 141, iw, 94, theme::kSubtleBg);
  (void)scard;
  key_name_box_ = new Fl_Box(ix + 48, 164, 500, 21, "");
  key_name_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  key_name_box_->labelfont(theme::font_for(theme::kWeightBold));
  key_name_box_->labelsize(theme::kFontBody);
  key_name_box_->labelcolor(theme::kText);
  auto* id_label = new Fl_Box(ix + 16, 198, 90, 17, "主密钥 ID");
  id_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  id_label->labelsize(theme::kFontLabel);
  id_label->labelcolor(theme::kTextMuted);
  key_id_box_ = new Fl_Box(ix + 114, 197, 400, 17, "");
  key_id_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  key_id_box_->labelfont(theme::font_for(theme::kWeightMedium));
  key_id_box_->labelsize(theme::kFontLabel);
  key_id_box_->labelcolor(theme::kText);

  auto* orig_label = new Fl_Box(ix, 261, 400, 17, "主密钥原密码");
  orig_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  orig_label->labelfont(theme::font_for(theme::kWeightRegular));
  orig_label->labelsize(theme::kFontLabel);
  orig_label->labelcolor(theme::kText);

  original_ = new widgets::Field(ix, 284, iw, 46);
  original_->type(FL_SECRET_INPUT);

  auto* orig_note = new Fl_Box(ix, 342, iw, 16, "验证这把主密钥的当前密码；错误时导出不会开始。");
  orig_note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  orig_note->labelsize(theme::kFontSmall);
  orig_note->labelcolor(theme::kTextMuted);

  auto* prot_label = new Fl_Box(ix, 383, 400, 17, "导出文件保护密码");
  prot_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  prot_label->labelfont(theme::font_for(theme::kWeightRegular));
  prot_label->labelsize(theme::kFontLabel);
  prot_label->labelcolor(theme::kText);

  protection_ = new widgets::Field(ix, 406, iw, 46);
  protection_->type(FL_SECRET_INPUT);

  auto* prot_note = new Fl_Box(ix, 466, iw, 16, "为这份备份新设密码；导入该文件时使用。");
  prot_note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  prot_note->labelsize(theme::kFontSmall);
  prot_note->labelcolor(theme::kTextMuted);

  auto* confirm_label = new Fl_Box(ix, 505, 400, 17, "确认保护密码");
  confirm_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  confirm_label->labelfont(theme::font_for(theme::kWeightRegular));
  confirm_label->labelsize(theme::kFontLabel);
  confirm_label->labelcolor(theme::kText);

  confirm_ = new widgets::Field(ix, 528, iw, 46);
  confirm_->type(FL_SECRET_INPUT);

  // 保存位置（05 号画板：标签 600、块 623,58、选择位置钮 635,72x34）。
  auto* loc_label = new Fl_Box(ix, 600, 200, 17, "保存位置");
  loc_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  loc_label->labelfont(theme::font_for(theme::kWeightRegular));
  loc_label->labelsize(theme::kFontLabel);
  loc_label->labelcolor(theme::kText);
  widgets::Card* lcard = new widgets::Card(ix, 623, iw, 58, theme::kSubtleBg);
  (void)lcard;
  location_box_ = new Fl_Box(ix + 16, 641, iw - 130, 21, "尚未选择位置");
  location_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  location_box_->labelsize(theme::kFontLabel);
  location_box_->labelcolor(theme::kText);
  choose_btn_ = new widgets::Button(ix + iw - 88, 635, 72, 34, "选择位置",
                                    ButtonKind::kSecondary);
  choose_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ExportKeyPage*>(d)->on_choose_location();
  }, this);

  // 黄色提醒（05 号画板 701,43，单行）。
  warning_ = make_notice(ix, 701, iw, widgets::Notice::Tone::kWarning, "",
                         "保护密码丢失后，这份主密钥备份无法导入。请将文件与密码分开妥善保存。",
                         43);

  // 绿色说明块（05 号画板 #E6F6F1 764,50）。
  widgets::Card* gcard = new widgets::Card(ix, 764, iw, 50, 0xE6F6F100);
  (void)gcard;
  info_ = new Fl_Box(ix + 40, 780, iw - 80, 20, "导出不会删除本机的主密钥。");
  info_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  info_->labelsize(theme::kFontLabel);
  info_->labelcolor(theme::kText);

  // 最终稿没有导出按钮；导出动作是功能必需，补在说明块下方。
  export_btn_ = new widgets::IconButton(ix, 830, 168, 44, Icon::kExport, "导出主密钥");
  export_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ExportKeyPage*>(d)->on_export();
  }, this);

  error_ = new Fl_Box(ix, 884, iw, 19, "");
  error_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  error_->labelsize(theme::kFontSmall);
  error_->labelcolor(theme::kDangerText);
  end();
}

void ExportKeyPage::reload() {
  key_id_ = host()->export_target();
  std::string name;
  for (const auto& k : host()->service().list_master_keys()) {
    if (k.master_key_id == key_id_) name = k.name;
  }
  key_name_box_->copy_label(name.empty() ? "(未命名主密钥)" : name.c_str());
  key_id_box_->copy_label(key_id_.c_str());
  error_->copy_label(last_error_.c_str());
  redraw();
}

std::string ExportKeyPage::status_text() const { return last_error_; }

void ExportKeyPage::on_choose_location() {
  const std::optional<std::string> picked =
      choose_save_file("导出主密钥", "*.smkexp", key_id_ + ".smkexp");
  if (!picked.has_value()) return;
  location_ = *picked;
  location_box_->copy_label(location_.c_str());
  redraw();
}

void ExportKeyPage::on_export() {
  last_error_.clear();
  if (key_id_.empty()) {
    last_error_ = "请先在主密钥管理中选择要导出的主密钥";
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  const std::string orig = original_->value() ? original_->value() : "";
  const std::string prot = protection_->value() ? protection_->value() : "";
  const std::string cfm = confirm_->value() ? confirm_->value() : "";
  if (orig.empty()) {
    last_error_ = "请输入主密钥原密码";
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  if (prot.empty()) {
    last_error_ = "请输入导出文件保护密码";
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  if (prot != cfm) {
    last_error_ = "两次输入的密码不一致";
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  if (location_.empty()) {
    const std::optional<std::string> picked =
        choose_save_file("导出主密钥", "*.smkexp", key_id_ + ".smkexp");
    if (!picked.has_value()) return;
    location_ = *picked;
  }

  std::string original = orig;
  std::string protection = prot;
  std::vector<std::uint8_t> bytes;
  const svc::Error rc =
      host()->service().export_master_key(key_id_, as_bytes(original),
                                          as_bytes(protection), &bytes);
  scrub(&original);
  original_->value("");
  protection_->value("");
  confirm_->value("");

  if (rc != svc::Error::kOk) {
    last_error_ = error_text(rc);
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  if (!write_bytes(location_, bytes)) {
    last_error_ = "文件读写失败";
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  host()->refresh();
  host()->set_status("主密钥已导出");
  host()->navigate(Page::kMasterKeys);
}


// =====================================================================
// 06 机密信息管理
// =====================================================================
SecretsPage::SecretsPage(MainWindow* host)
    : PageBase(host, "机密信息管理", "所有内容只保存在本机。") {
  begin();
  // 页头右侧动作（06 号画板）：导入白底 72x34 + 新增主色 146x44。
  imp_btn_ = new widgets::IconButton(kWinW - theme::kContentPad - 146 - 12 - 72, 44, 72, 34,
                                     Icon::kImport, "导入", ButtonKind::kSecondary);
  imp_btn_->labelsize(theme::kFontSmall);
  add_btn_ = new widgets::IconButton(kWinW - theme::kContentPad - 146, 40, 146, 44, Icon::kPlus,
                                     "新增机密信息");
  imp_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_import();
  }, this);
  add_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_add();
  }, this);

  // 左列卡片（06 号画板 240,117,530,571 r12），搜索框嵌在卡片顶部。
  const int lx = theme::kSidebarWidth + theme::kContentPad;
  const int lw = 530;
  new widgets::Card(lx, 117, lw, 571);
  search_ = new widgets::Field(lx + 16, 117, lw - 32, 36);
  search_->set_placeholder("搜索标题或主密钥 ID");
  search_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->reload();
  }, this);

  // 行在 draw() 里绘制（行高 52，选中 #E8F4F0），点击命中在 handle()。

  // 右列详情卡片（06 号画板 790,117,378,621 r12）。
  const int dx = 790;
  const int dw = 378;
  const int ix = dx + 24;
  const int iw = dw - 48;
  new widgets::Card(dx, 117, dw, 621);

  detail_title_ = new Fl_Box(ix, 149, 220, 24, "信息详情");
  detail_title_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_title_->labelfont(theme::font_for(theme::kWeightBold));
  detail_title_->labelsize(17);
  detail_title_->labelcolor(theme::kText);
  verified_ = new widgets::Pill(dx + dw - 24 - 49, 141, 49, 21, "已验证",
                                theme::kAccent, theme::kAccentSoft);

  struct RowSpec { const char* label; int y; Fl_Box** target; int fs; };
  Fl_Box* mk_id_l = new Fl_Box(ix, 195, 98, 16, "主密钥 ID");
  mk_id_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  mk_id_l->labelsize(theme::kFontLabel);
  mk_id_l->labelcolor(theme::kTextMuted);
  detail_mk_id_ = new Fl_Box(ix + 98, 194, iw - 98, 18, "");
  detail_mk_id_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_mk_id_->labelfont(theme::font_for(theme::kWeightMedium));
  detail_mk_id_->labelsize(theme::kFontLabel);
  detail_mk_id_->labelcolor(theme::kText);

  Fl_Box* mk_name_l = new Fl_Box(ix, 231, 98, 16, "主密钥名称");
  mk_name_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  mk_name_l->labelsize(theme::kFontLabel);
  mk_name_l->labelcolor(theme::kTextMuted);
  detail_mk_name_ = new Fl_Box(ix + 98, 228, iw - 98, 22, "");
  detail_mk_name_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_mk_name_->labelsize(theme::kFontBody);
  detail_mk_name_->labelcolor(theme::kText);

  Fl_Box* t_l = new Fl_Box(ix, 267, 98, 16, "标题");
  t_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  t_l->labelsize(theme::kFontLabel);
  t_l->labelcolor(theme::kTextMuted);
  detail_title_field_ = new Fl_Box(ix + 98, 264, iw - 98, 22, "");
  detail_title_field_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_title_field_->labelsize(theme::kFontBody);
  detail_title_field_->labelcolor(theme::kText);

  // 分隔线（06 号画板 306）。
  Fl_Box* div = new Fl_Box(ix, 306, iw, 1, "");
  div->box(FL_FLAT_BOX);
  div->color(theme::kBorder);

  Fl_Box* secret_l = new Fl_Box(ix, 338, 160, 18, "机密信息");
  secret_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  secret_l->labelsize(theme::kFontLabel);
  secret_l->labelcolor(theme::kText);
  reveal_btn_ = new widgets::IconButton(ix + iw - 72, 325, 72, 34, Icon::kEyeOff, "隐藏",
                                        ButtonKind::kSecondary);
  reveal_btn_->labelsize(theme::kFontSmall);
  reveal_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_toggle_reveal();
  }, this);

  // 明文区（06 号画板 #F8FAFA 371,145 r8）。
  plaintext_box_ = new Fl_Box(ix, 371, iw, 145, svc::kMaskedPlaintext);
  plaintext_box_->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  plaintext_box_->box(FL_FLAT_BOX);
  plaintext_box_->color(theme::kSubtleBg);
  plaintext_box_->labelsize(theme::kFontBody);
  plaintext_box_->labelcolor(theme::kText);
  plaintext_box_->labelfont(theme::font_for(theme::kWeightRegular));

  plaintext_meta_ = new Fl_Box(ix, 533, iw, 16, "");
  plaintext_meta_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  plaintext_meta_->labelsize(theme::kFontTiny);
  plaintext_meta_->labelcolor(theme::kTextMuted);

  // 操作行（06 号画板 564,34）：复制主色 72 / 导出白底 72 / 删除浅红 48。
  copy_btn_ = new widgets::IconButton(ix, 564, 72, 34, Icon::kCopy, "复制");
  copy_btn_->labelsize(theme::kFontSmall);
  exp_btn_ = new widgets::IconButton(ix + 80, 564, 72, 34, Icon::kExport, "导出",
                                     ButtonKind::kSecondary);
  exp_btn_->labelsize(theme::kFontSmall);
  del_btn_ = new widgets::IconButton(ix + 160, 564, 48, 34, Icon::kTrash, "删除",
                                     ButtonKind::kDanger);
  del_btn_->labelsize(theme::kFontSmall);
  copy_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_copy();
  }, this);
  exp_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_export();
  }, this);
  del_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_delete();
  }, this);

  // 绿色说明块（06 号画板 #E8F4F0 616,62，次要色文字）。
  hide_notice_ = new widgets::Card(ix, 616, iw, 62, theme::kAccentSoft);
  hide_notice_text_ = new Fl_Box(ix + 16, 630, iw - 32, 36,
                                 "默认隐藏明文；离开窗口、失焦、最小化\n或切换界面时，立即重新掩码。");
  hide_notice_text_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  hide_notice_text_->labelsize(theme::kFontTiny);
  hide_notice_text_->labelcolor(theme::kTextMuted);

  // 左列底部计数行（06 号画板 627）+ 排序标签。
  count_label_ = new Fl_Box(lx + 16, 627, 260, 16, "");
  count_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  count_label_->labelsize(theme::kFontTiny);
  count_label_->labelcolor(theme::kTextMuted);
  sort_label_ = new Fl_Box(lx + 16, 627, lw - 32, 16, "最近添加 ↓");
  sort_label_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
  sort_label_->labelsize(theme::kFontTiny);
  sort_label_->labelcolor(theme::kTextMuted);
  missing_label_ = new Fl_Box(lx + 16, 627, lw - 120, 16, "");
  missing_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  missing_label_->labelsize(theme::kFontTiny);
  missing_label_->labelcolor(theme::kTextMuted);

  // 页脚说明（06 号画板 board 804 → 客户区 768）。
  Fl_Box* foot = new Fl_Box(theme::kSidebarWidth + theme::kContentPad, 768, 700, 19,
                            "ID 后的黄色 ? 表示主密钥缺失；名称为空不代表缺失。");
  foot->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  foot->labelsize(theme::kFontSmall);
  foot->labelcolor(theme::kTextMuted);
  end();
}

void SecretsPage::reload() {
  const auto items = host()->service().list_secrets();
  const auto quota = host()->service().quota();

  std::string query = search_->value() ? search_->value() : "";
  ids_.clear();
  std::vector<widgets::ListRow> rows;
  int missing = 0;
  for (const auto& item : items) {
    if (!query.empty() && item.title.find(query) == std::string::npos &&
        item.master_key_id.find(query) == std::string::npos) {
      continue;
    }
    const auto projected = project_secrets({item});
    widgets::ListRow row;
    row.c0 = projected[0].c0;
    row.c1 = projected[0].c1;
    row.c2 = projected[0].c2;
    row.show_question_mark = projected[0].show_question_mark;
    rows.push_back(row);
    ids_.push_back(item.secret_id);
    if (!item.master_key_found) ++missing;
  }
  rows_ = rows;

  count_label_->copy_label(
      (std::to_string(items.size()) + " / " + std::to_string(svc::kMaxSecrets) + " 条" +
       (missing > 0 ? "　·　" : ""))
          .c_str());
  set_subtitle("所有内容只保存在本机。共 " + std::to_string(items.size()) + " 条，基础版最多 " +
               std::to_string(svc::kMaxSecrets) + " 条。");
  missing_label_->copy_label(missing > 0
                                 ? (std::to_string(missing) + " 条缺失主密钥").c_str()
                                 : "");

  add_btn_->set_enabled(quota.secrets < svc::kMaxSecrets);
  imp_btn_->set_enabled(quota.secrets < svc::kMaxSecrets);

  // 保持选中：优先已选中的 id，否则默认第一行。
  int target = -1;
  for (std::size_t i = 0; i < ids_.size(); ++i) {
    if (ids_[i] == selected_id_) target = static_cast<int>(i);
  }
  if (target < 0 && !ids_.empty()) target = 0;
  selected_row_ = target;
  if (target >= 0) {
    selected_id_ = ids_[static_cast<std::size_t>(target)];
  } else {
    selected_id_.clear();
  }
  refresh_detail();
  redraw();
}

std::string SecretsPage::status_text() const { return last_error_; }

void SecretsPage::on_row_select() { on_row_click(selected_row_); }

void SecretsPage::on_row_click(int row) {
  if (row < 0 || row >= static_cast<int>(ids_.size())) {
    selected_id_.clear();
    refresh_detail();
    return;
  }
  selected_row_ = row;
  selected_id_ = ids_[static_cast<std::size_t>(row)];
  hide_plaintext();
  refresh_detail();
}

void SecretsPage::refresh_detail() {
  const auto items = host()->service().list_secrets();
  const svc::SecretListItem* found = nullptr;
  for (const auto& item : items) {
    if (item.secret_id == selected_id_) found = &item;
  }
  if (found == nullptr) {
    detail_mk_id_->copy_label("");
    detail_mk_name_->copy_label("");
    detail_title_field_->copy_label("");
    verified_->set_shown(false);
    verified_->redraw();
    reveal_btn_->set_enabled(false);
    copy_btn_->set_enabled(false);
    exp_btn_->set_enabled(false);
    del_btn_->set_enabled(false);
    plaintext_box_->copy_label("");
    plaintext_meta_->copy_label("");
    return;
  }
  detail_mk_id_->copy_label(found->master_key_id.c_str());
  detail_mk_name_->copy_label(found->master_key_name.c_str());
  detail_title_field_->copy_label(found->title.c_str());
  reveal_btn_->set_enabled(found->master_key_found);
  copy_btn_->set_enabled(found->master_key_found);
  exp_btn_->set_enabled(found->master_key_found);
  del_btn_->set_enabled(true);
  if (found->master_key_found) {
    verified_->copy_label("已验证");
    verified_->labelcolor(theme::kAccent);
    verified_->set_shown(true);
  } else {
    verified_->copy_label("缺少主密钥");
    verified_->labelcolor(theme::kWarningText);
    verified_->set_shown(true);
  }
  verified_->redraw();
  hide_plaintext();
}

void SecretsPage::refresh_meta() {
  plaintext_meta_->copy_label(revealed_ ? "已显示明文，离开窗口将自动掩码" : "");
  plaintext_meta_->redraw();
}

void SecretsPage::on_toggle_reveal() {
  if (selected_id_.empty()) {
    last_error_ = "请先选择机密信息";
    return;
  }
  if (revealed_) {
    hide_plaintext();
    return;
  }
  std::string id = selected_id_;
  std::string key_id;
  bool found = false;
  for (const auto& item : host()->service().list_secrets()) {
    if (item.secret_id == id) {
      key_id = item.master_key_id;
      found = item.master_key_found;
    }
  }
  if (!found) {
    // 主密钥缺失时按需求 3.5 只显示 6 个星号，不索要密码。
    plaintext_box_->copy_label(svc::kMaskedPlaintext);
    plaintext_box_->redraw();
    last_error_ = "该机密信息的主密钥不在本地";
    return;
  }
  std::string text;
  svc::Error rc = host()->service().reveal_secret_plaintext(id, {}, &text);
  if (rc == svc::Error::kNeedsPassword) {
    PasswordPrompt p;
    std::string body = "主密钥 ID: " + key_id + "\n请输入该主密钥密码";
    ask_password("查看机密信息", body.c_str(), "主密钥密码", nullptr, &p);
    if (!p.accepted) return;
    std::string password = p.first;
    rc = host()->service().reveal_secret_plaintext(id, as_bytes(password), &text);
    scrub(&password);
  }
  if (rc != svc::Error::kOk) {
    last_error_ = error_text(rc);
    return;
  }
  plaintext_ = text;
  revealed_ = true;
  plaintext_box_->copy_label(plaintext_.c_str());
  plaintext_box_->redraw();
  reveal_btn_->copy_label("隐藏");
  reveal_btn_->redraw();
  refresh_meta();
  host()->note_activity();
}

void SecretsPage::on_copy() {
  if (!revealed_ || plaintext_.empty()) {
    last_error_ = "请先查看机密信息";
    return;
  }
  if (!clipboard_set(plaintext_)) {
    last_error_ = "剪贴板不可用";
    return;
  }
  last_error_ = "已复制，" + std::to_string(host()->settings().clipboard_clear_seconds) +
                " 秒后自动清除";
  host()->arm_clipboard_clear();
}

void SecretsPage::on_export() {
  if (selected_id_.empty()) {
    last_error_ = "请先选择机密信息";
    return;
  }
  const std::optional<std::string> path =
      choose_save_file("导出机密信息", "*.sscexp", selected_id_ + ".sscexp");
  if (!path.has_value()) return;
  std::vector<std::uint8_t> bytes;
  std::string id = selected_id_;
  std::string key_id;
  for (const auto& item : host()->service().list_secrets()) {
    if (item.secret_id == id) key_id = item.master_key_id;
  }
  svc::Error rc = host()->service().export_secret(id, {}, &bytes);
  if (rc == svc::Error::kNeedsPassword) {
    PasswordPrompt p;
    ask_password("导出机密信息", ("主密钥 ID: " + key_id + "\n请输入该主密钥密码").c_str(),
                 "主密钥密码", nullptr, &p);
    if (!p.accepted) return;
    std::string password = p.first;
    rc = host()->service().export_secret(id, as_bytes(password), &bytes);
    scrub(&password);
  }
  if (rc != svc::Error::kOk) {
    last_error_ = error_text(rc);
    return;
  }
  if (!write_bytes(*path, bytes)) {
    last_error_ = "文件读写失败";
    return;
  }
  last_error_ = "机密信息已导出";
}

void SecretsPage::on_delete() {
  if (selected_id_.empty()) {
    last_error_ = "请先选择机密信息";
    return;
  }
  if (fl_choice("%s", "取消", "删除", nullptr) != 1) return;
  std::string id = selected_id_;
  std::string key_id;
  for (const auto& item : host()->service().list_secrets()) {
    if (item.secret_id == id) key_id = item.master_key_id;
  }
  svc::Error rc = host()->service().delete_secret(id, {});
  if (rc == svc::Error::kNeedsPassword) {
    PasswordPrompt p;
    ask_password("删除机密信息", ("主密钥 ID: " + key_id + "\n请输入该主密钥密码").c_str(),
                 "主密钥密码", nullptr, &p);
    if (!p.accepted) return;
    std::string password = p.first;
    rc = host()->service().delete_secret(id, as_bytes(password));
    scrub(&password);
  }
  if (rc != svc::Error::kOk) {
    last_error_ = error_text(rc);
    return;
  }
  selected_id_.clear();
  hide_plaintext();
  host()->refresh();
  last_error_ = "机密信息已删除";
}

void SecretsPage::hide_plaintext() {
  if (!plaintext_.empty()) scrub(&plaintext_);
  revealed_ = false;
  plaintext_box_->copy_label(selected_id_.empty() ? "" : svc::kMaskedPlaintext);
  plaintext_box_->redraw();
  reveal_btn_->copy_label("隐藏");
  reveal_btn_->redraw();
  refresh_meta();
}

void SecretsPage::draw() {
  PageBase::draw();

  const int lx = theme::kSidebarWidth + theme::kContentPad;
  const int lw = 530;
  // 搜索框左侧的放大镜
  widgets::draw_icon(widgets::Icon::kSearch, lx + 26, 125, theme::kTextFaint);

  // 表头行（06 号画板 #F8FAFA，三列）。
  const int head_y = 153;
  fl_color(theme::kSubtleBg);
  fl_rectf(lx + 1, head_y, lw - 2, 37);
  fl_color(theme::kTextMuted);
  fl_font(theme::font_for(theme::kWeightRegular), theme::kFontTiny);
  fl_draw("标题（可选）", lx + 16, head_y, 200, 37, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  fl_draw("主密钥 ID", lx + 216, head_y, 124, 37, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  fl_draw("主密钥名称", lx + 340, head_y, 174, 37, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  // 数据行：行高 52，选中 #E8F4F0 + 主色描边。
  int y = 190;
  for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
    const widgets::ListRow& row = rows_[static_cast<std::size_t>(r)];
    const bool sel = r == selected_row_;
    theme::fill_rounded(lx, y, lw, 52, 0, sel ? theme::kAccentSoft : theme::kCardBg);
    theme::stroke_rounded(lx, y, lw, 52, 0, sel ? theme::kAccent : theme::kBorder);
    fl_color(sel ? theme::kAccent : theme::kText);
    fl_font(theme::font_for(theme::kWeightRegular), theme::kFontLabel);
    fl_draw(row.c0.c_str(), lx + 16, y, 200, 52, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    fl_color(theme::kText);
    fl_font(theme::font_for(theme::kWeightMedium), theme::kFontSmall);
    fl_draw(row.c1.c_str(), lx + 216, y, 110, 52, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    if (row.show_question_mark) {
      // 黄色问号紧贴 ID（需求 3.1 / 3.5）。
      fl_color(theme::kWarningBorder);
      fl_font(theme::font_for(theme::kWeightBold), theme::kFontSmall);
      fl_draw("?", lx + 216 + static_cast<int>(fl_width(row.c1.c_str())) + 2, y, 14, 52,
              FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }
    fl_color(theme::kTextMuted);
    fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
    fl_draw(row.c2.c_str(), lx + 340, y, 174, 52, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 52;
  }
}

int SecretsPage::handle(int event) {
  // 左列行无子控件，命中检测在页面层完成（06 号画板）。
  if (event == FL_PUSH) {
    const int px = Fl::event_x();
    const int py = Fl::event_y();
    const int lx = theme::kSidebarWidth + theme::kContentPad;
    if (px >= lx && px <= lx + 530 && py >= 190) {
      const int row = (py - 190) / 52;
      if (row < static_cast<int>(rows_.size())) {
        on_row_click(row);
        return 1;
      }
    }
  }
  return Fl_Group::handle(event);
}

void SecretsPage::on_add() { host()->navigate(Page::kAddSecret); }

void SecretsPage::on_import() { host()->navigate(Page::kImportSecret); }


// =====================================================================
// 07 新增机密信息
// =====================================================================
AddSecretPage::AddSecretPage(MainWindow* host)
    : PageBase(host, "新增机密信息", "保存任何你需要保密的文字，不限定内容格式。") {
  begin();
  layout_header("返回列表");

  // 07 号画板：白卡（240,117,928,576 r12）。
  new widgets::Card(theme::kSidebarWidth + theme::kContentPad, 117, 928, 576);
  const int ix = 264;
  const int iw = 880;

  auto* key_label = new Fl_Box(ix, 147, 400, 17, "用于加密的主密钥");
  key_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  key_label->labelfont(theme::font_for(theme::kWeightRegular));
  key_label->labelsize(theme::kFontLabel);
  key_label->labelcolor(theme::kText);

  selector_ = new widgets::KeySelector(ix, 170, iw, 65);
  selector_->set_placeholder("尚未创建主密钥");
  selector_->callback([](Fl_Widget*, void* data) {
    static_cast<AddSecretPage*>(data)->on_pick_key();
  }, this);

  auto* title_label = new Fl_Box(ix, 261, 400, 17, "标题（可选）");
  title_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  title_label->labelfont(theme::font_for(theme::kWeightRegular));
  title_label->labelsize(theme::kFontLabel);
  title_label->labelcolor(theme::kText);

  title_ = new widgets::Field(ix, 284, iw, 46);
  title_->set_placeholder("不填写则保留为空");

  auto* content_label = new Fl_Box(ix, 356, 400, 17, "机密信息");
  content_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  content_label->labelfont(theme::font_for(theme::kWeightRegular));
  content_label->labelsize(theme::kFontLabel);
  content_label->labelcolor(theme::kText);

  content_ = new widgets::TextArea(ix, 379, iw, 136);

  // 计数行（07 号画板 529）：左侧格式提示，右侧 N / 150。
  auto* hint = new Fl_Box(ix, 529, 400, 16, "自由文本，最多 150 字符");
  hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  hint->labelsize(theme::kFontSmall);
  hint->labelcolor(theme::kTextMuted);
  counter_ = new Fl_Box(ix + 400, 525, iw - 400, 20, "0 / 150");
  counter_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
  counter_->labelsize(theme::kFontSmall);
  counter_->labelcolor(theme::kTextMuted);

  // 绿色说明（07 号画板 #E8F4F0 562,43）。
  info_ = make_notice(ix, 562, iw, widgets::Notice::Tone::kSuccess, "",
                      "使用当前默认主密钥在本机加密，不会上传任何内容。", 43);

  // 按钮（07 号画板 625,44）：保存主色 + 取消白底。
  save_ = new widgets::IconButton(ix, 625, 132, 44, Icon::kCheck, "保存");
  save_->callback([](Fl_Widget*, void* d) {
    static_cast<AddSecretPage*>(d)->on_save();
  }, this);
  auto* cancel = new widgets::Button(ix + 144, 625, 64, 44, "取消", ButtonKind::kSecondary);
  cancel->callback([](Fl_Widget*, void* d) {
    static_cast<PageBase*>(static_cast<AddSecretPage*>(d))->on_back();
  }, this);

  error_ = new Fl_Box(ix, 679, iw, 19, "");
  error_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  error_->labelsize(theme::kFontSmall);
  error_->labelcolor(theme::kDangerText);
  end();
}

void AddSecretPage::reload() {
  const auto keys = host()->service().list_master_keys();
  if (key_id_.empty()) {
    const std::optional<std::string> def = host()->service().default_master_key_id();
    key_id_ = def.value_or(keys.empty() ? "" : keys[0].master_key_id);
  }
  for (const auto& k : keys) {
    if (k.master_key_id == key_id_) {
      selector_->set_key(k.name, k.master_key_id, k.is_default);
      break;
    }
  }
  save_->set_enabled(!keys.empty());
  const char* v = content_->value();
  counter_->copy_label(char_counter_text(v ? v : "").c_str());
  error_->copy_label(last_error_.c_str());
  redraw();
}

std::string AddSecretPage::status_text() const {
  return last_error_.empty() ? last_status_ : last_error_;
}

void AddSecretPage::on_pick_key() {
  const auto keys = host()->service().list_master_keys();
  if (keys.empty()) return;
  int index = 0;
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (keys[i].master_key_id == key_id_) index = static_cast<int>(i);
  }
  index = (index + 1) % static_cast<int>(keys.size());
  key_id_ = keys[static_cast<std::size_t>(index)].master_key_id;
  for (const auto& k : keys) {
    if (k.master_key_id == key_id_) {
      selector_->set_key(k.name, k.master_key_id, k.is_default);
      break;
    }
  }
  redraw();
}

void AddSecretPage::on_save() {
  last_error_.clear();
  if (key_id_.empty()) {
    last_error_ = "请先选择主密钥";
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  const char* raw = content_->value();
  const std::string plaintext = raw ? raw : "";
  if (plaintext.empty()) {
    last_error_ = error_text(svc::Error::kEmptySecret);
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  const std::string title = title_->value() ? title_->value() : "";

  svc::Error rc = host()->service().add_secret(title, plaintext, key_id_, {});
  if (rc == svc::Error::kNeedsPassword) {
    PasswordPrompt p;
    ask_password("保存机密信息",
                 ("主密钥 ID: " + key_id_ + "\n请输入该主密钥密码").c_str(), "主密钥密码",
                 nullptr, &p);
    if (!p.accepted) return;
    std::string password = p.first;
    rc = host()->service().add_secret(title, plaintext, key_id_, as_bytes(password));
    scrub(&password);
  }
  content_->value("");
  title_->value("");
  if (rc != svc::Error::kOk) {
    last_error_ = error_text(rc);
    error_->copy_label(last_error_.c_str());
    redraw();
    return;
  }
  host()->refresh();
  host()->set_status("机密信息已保存");
  host()->navigate(Page::kSecrets);
}

// =====================================================================
// 08 导入机密信息
// =====================================================================
ImportSecretPage::ImportSecretPage(MainWindow* host)
    : PageBase(host, "导入机密信息", "读取本地加密文件，自动查找它使用的主密钥。") {
  begin();
  layout_header(nullptr);

  // 08 号画板：白卡（240,117,928,623 r12）。
  new widgets::Card(theme::kSidebarWidth + theme::kContentPad, 117, 928, 623);
  const int ix = 264;
  const int iw = 880;

  // 三段步骤条（08 号画板 141,21）。
  step1_ = new widgets::Pill(ix, 141, 71, 21, "文件已选择", theme::kAccent,
                             theme::kAccentSoft);
  step2_ = new widgets::Pill(ix + 113, 141, 71, 21, "验证主密钥", theme::kAccent,
                             theme::kAccentSoft);
  step3_ = new widgets::Pill(ix + 226, 141, 71, 21, "保存到本机", theme::kTextMuted,
                             theme::kSubtleBg);

  // 文件块（08 号画板 #F8FAFA+描边 182,199 r12），内容水平居中。
  widgets::Card* fcard = new widgets::Card(ix, 182, iw, 199, theme::kSubtleBg);
  (void)fcard;
  file_name_ = new Fl_Box(ix, 261, iw, 21, "尚未选择文件");
  file_name_->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
  file_name_->labelfont(theme::font_for(theme::kWeightRegular));
  file_name_->labelsize(theme::kFontBody);
  file_name_->labelcolor(theme::kText);
  file_sub_ = new Fl_Box(ix, 294, iw, 17, "选择之前导出的 .secret 加密文件");
  file_sub_->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
  file_sub_->labelsize(theme::kFontSmall);
  file_sub_->labelcolor(theme::kTextMuted);
  choose_btn_ = new widgets::Button(ix + (iw - 96) / 2, 319, 96, 34, "选择文件",
                                    ButtonKind::kSecondary);
  choose_btn_->labelsize(theme::kFontSmall);
  choose_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ImportSecretPage*>(d)->on_choose_file();
  }, this);

  // 匹配的主密钥块（08 号画板 #F8FAFA 401,129 r8）。
  widgets::Card* mcard = new widgets::Card(ix, 401, iw, 129, theme::kSubtleBg);
  (void)mcard;
  match_title_ = new Fl_Box(ix + 16, 419, 300, 17, "匹配的主密钥");
  match_title_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  match_title_->labelfont(theme::font_for(theme::kWeightRegular));
  match_title_->labelsize(theme::kFontLabel);
  match_title_->labelcolor(theme::kText);
  auto* mid_l = new Fl_Box(ix + 16, 453, 98, 17, "主密钥 ID");
  mid_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  mid_l->labelsize(theme::kFontLabel);
  mid_l->labelcolor(theme::kTextMuted);
  match_id_ = new Fl_Box(ix + 114, 452, 400, 17, "");
  match_id_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  match_id_->labelfont(theme::font_for(theme::kWeightMedium));
  match_id_->labelsize(theme::kFontLabel);
  match_id_->labelcolor(theme::kText);
  auto* mname_l = new Fl_Box(ix + 16, 491, 98, 17, "主密钥名称");
  mname_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  mname_l->labelsize(theme::kFontLabel);
  mname_l->labelcolor(theme::kTextMuted);
  match_name_ = new Fl_Box(ix + 114, 489, 400, 19, "");
  match_name_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  match_name_->labelsize(theme::kFontBody);
  match_name_->labelcolor(theme::kText);

  // 密码字段（08 号画板：标签 556、输入 579,46、说明 639）。
  password_label_ = new Fl_Box(ix, 556, 600, 17, "主密钥密码");
  password_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  password_label_->labelfont(theme::font_for(theme::kWeightRegular));
  password_label_->labelsize(theme::kFontLabel);
  password_label_->labelcolor(theme::kText);
  key_password_ = new widgets::Field(ix, 579, iw, 46);
  key_password_->type(FL_SECRET_INPUT);
  match_hint_ = new Fl_Box(ix, 639, iw, 16, "非默认主密钥，需验证其密码后导入。");
  match_hint_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  match_hint_->labelsize(theme::kFontSmall);
  match_hint_->labelcolor(theme::kTextMuted);

  // 按钮（08 号画板 672,44）：导入主色 + 取消白底。
  import_btn_ = new widgets::IconButton(ix, 672, 132, 44, Icon::kImport, "导入");
  import_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ImportSecretPage*>(d)->on_import();
  }, this);
  auto* cancel = new widgets::Button(ix + 144, 672, 64, 44, "取消", ButtonKind::kSecondary);
  cancel->callback([](Fl_Widget*, void* d) {
    static_cast<PageBase*>(static_cast<ImportSecretPage*>(d))->on_back();
  }, this);

  error_ = new Fl_Box(ix, 726, iw, 19, "");
  error_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  error_->labelsize(theme::kFontSmall);
  error_->labelcolor(theme::kDangerText);
  end();
}

void ImportSecretPage::reload() {
  import_btn_->set_enabled(!path_.empty());
  if (path_.empty()) {
    file_name_->copy_label("尚未选择文件");
    file_sub_->copy_label("");
    match_title_->copy_label("匹配的主密钥");
    match_id_->copy_label("");
    match_name_->copy_label("");
    match_hint_->copy_label("");
    key_password_->hide();
    error_->copy_label("");
    redraw();
    return;
  }
  const std::string base = fs::path(path_).filename().string();
  file_name_->copy_label(base.c_str());
  file_sub_->copy_label("本地文件 · 已选择");

  // 读取文件头里的主密钥 ID（service 会在导入时报缺失，这里只做展示）。
  const std::optional<std::vector<std::uint8_t>> bytes = read_bytes(path_);
  key_found_ = false;
  key_id_.clear();
  if (bytes.has_value()) {
    svc::Error probe = host()->service().import_secret(*bytes, {}, {});
    key_found_ = (probe == svc::Error::kOk);
  }
  match_title_->copy_label(key_found_ ? "匹配的主密钥" : "找不到主密钥");
  if (key_found_) {
    for (const auto& k : host()->service().list_master_keys()) {
      (void)k;
    }
    match_id_->copy_label(key_id_.c_str());
    match_name_->copy_label("");
    match_hint_->copy_label("");
  } else {
    match_id_->copy_label("");
    match_name_->copy_label("");
    match_hint_->copy_label("请先导入主密钥");
  }
  key_password_->show();
  password_label_->copy_label(
      key_found_ ? ("主密钥 " + key_id_ + " 的密码").c_str() : "主密钥密码");
  error_->copy_label(last_error_.c_str());
  redraw();
}

std::string ImportSecretPage::status_text() const { return last_error_; }

void ImportSecretPage::on_choose_file() {
  const std::optional<std::string> picked =
      choose_open_file("导入机密信息", "*.sscexp");
  if (!picked.has_value()) return;
  path_ = *picked;
  reload();
}

void ImportSecretPage::on_import() {
  last_error_.clear();
  const std::optional<std::vector<std::uint8_t>> bytes = read_bytes(path_);
  if (!bytes.has_value()) {
    last_error_ = "文件读写失败";
    return;
  }
  const std::string file_pwd = key_password_->value() ? key_password_->value() : "";
  std::string file_password = file_pwd;
  svc::Error rc = host()->service().import_secret(*bytes, as_bytes(file_password), {});
  if (rc == svc::Error::kNeedsPassword) {
    PasswordPrompt p;
    ask_password("导入机密信息", "请输入该主密钥的密码", "主密钥密码", nullptr, &p);
    if (!p.accepted) {
      scrub(&file_password);
      return;
    }
    std::string key_pwd = p.first;
    rc = host()->service().import_secret(*bytes, as_bytes(file_password), as_bytes(key_pwd));
    scrub(&key_pwd);
  }
  scrub(&file_password);
  key_password_->value("");
  if (rc != svc::Error::kOk) {
    last_error_ = error_text(rc);
    return;
  }
  host()->refresh();
  host()->set_status("机密信息已导入");
  host()->navigate(Page::kSecrets);
}

// =====================================================================
// 09 安全设置
// =====================================================================
SettingsPage::SettingsPage(MainWindow* host)
    : PageBase(host, "安全设置", "减少明文暴露，让每次离开都安心。设置仅保存在本机。") {
  begin();
  // 09 号画板：白卡（240,117,928,453 r12）。
  const int ix = theme::kSidebarWidth + theme::kContentPad;
  const int iw = 880;
  new widgets::Card(ix, 117, iw, 453);

  auto* title = new Fl_Box(ix + 24, 149, 300, 24, "锁定与隐私");
  title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  title->labelfont(theme::font_for(theme::kWeightBold));
  title->labelsize(17);
  title->labelcolor(theme::kText);

  struct ItemDef {
    const char* label;
    const char* desc;
    int y;
  };
  const ItemDef items[3] = {
      {"闲置自动锁定", "锁定时清空内存密钥缓存，并隐藏明文。", 197},
      {"剪贴板定时清除", "复制机密信息后，自动清除剪贴板内容。", 293},
      {"离开窗口隐藏明文", "失焦、最小化或切换界面时，立即重新掩码。", 389},
  };
  for (const ItemDef& item : items) {
    Fl_Box* l = new Fl_Box(ix + 24, item.y, 400, 20, item.label);
    l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    l->labelsize(theme::kFontBody);
    l->labelcolor(theme::kText);
    Fl_Box* d = new Fl_Box(ix + 24, item.y + 26, 560, 17, item.desc);
    d->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    d->labelsize(theme::kFontSmall);
    d->labelcolor(theme::kTextMuted);
  }

  // 数值选择（09 号画板 #F8FAFA 圆角块，点击循环切换）。
  idle_value_ = new widgets::Pill(1065, 198, 79, 36, "5 分钟", theme::kText,
                                  theme::kSubtleBg);
  clipboard_value_ = new widgets::Pill(1071, 294, 73, 36, "30 秒", theme::kText,
                                       theme::kSubtleBg);
  idle_value_->callback([](Fl_Widget*, void* d) {
    static_cast<SettingsPage*>(d)->on_idle_choice(0);
  }, this);
  clipboard_value_->callback([](Fl_Widget*, void* d) {
    static_cast<SettingsPage*>(d)->on_clipboard_choice(0);
  }, this);

  // 行分隔线（09 号画板 263 / 359 / 455）。
  for (int i = 0; i < 3; ++i) {
    Fl_Box* div = new Fl_Box(ix + 24, 263 + i * 96, iw - 48, 1, "");
    div->box(FL_FLAT_BOX);
    div->color(theme::kBorder);
  }

  auto* tip = new Fl_Box(ix + 24, 469, iw - 48, 15,
                         "时长为设计示例，可选：锁定 1 / 5 / 15 分钟；剪贴板 15 / 30 / 60 秒。");
  tip->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  tip->labelsize(theme::kFontTiny);
  tip->labelcolor(theme::kTextMuted);

  // 立即锁定（09 号画板白底 502,146x44）。
  auto* lock_now = new widgets::Button(ix, 502, 146, 44, "立即锁定密匣",
                                       ButtonKind::kSecondary);
  lock_now->callback([](Fl_Widget*, void* d) {
    static_cast<SettingsPage*>(d)->on_lock_now();
  }, this);
  end();
}

int SettingsPage::switch_geometry(int* x, int* y, int* w, int* h) const {
  if (x != nullptr) *x = 1104;
  if (y != nullptr) *y = 396;
  if (w != nullptr) *w = 40;
  if (h != nullptr) *h = 24;
  return 0;
}

void SettingsPage::draw() {
  PageBase::draw();
  // 拨动开关（09 号画板 40x24 r100，开=主色底白钮，关=灰色底）。
  int sx, sy, sw, sh;
  switch_geometry(&sx, &sy, &sw, &sh);
  const bool on = host()->settings().hide_plaintext_on_blur;
  theme::fill_rounded(sx, sy, sw, sh, sh / 2, on ? theme::kAccent : theme::kBorder);
  const int knob = 18;
  const int kx = on ? (sx + sw - knob - 3) : (sx + 3);
  theme::fill_rounded(kx, sy + 3, knob, knob, knob / 2, 0xFFFFFF00);
}

int SettingsPage::handle(int event) {
  if (event == FL_PUSH) {
    int sx, sy, sw, sh;
    switch_geometry(&sx, &sy, &sw, &sh);
    const int px = Fl::event_x();
    const int py = Fl::event_y();
    if (px >= sx - 6 && px <= sx + sw + 6 && py >= sy - 6 && py <= sy + sh + 6) {
      on_blur_toggle(false);
      return 1;
    }
  }
  return Fl_Group::handle(event);
}

void SettingsPage::reload() {
  const SecuritySettings& s = host()->settings();
  idle_value_->copy_label((std::to_string(s.idle_lock_minutes) + " 分钟").c_str());
  clipboard_value_->copy_label((std::to_string(s.clipboard_clear_seconds) + " 秒").c_str());
  if (s.hide_plaintext_on_blur) {

  } else {

  }
  idle_value_->redraw();
  clipboard_value_->redraw();
  redraw();
}

std::string SettingsPage::status_text() const { return {}; }

void SettingsPage::on_idle_choice(int) {
  SecuritySettings& s = host()->mutable_settings();
  const int choices[3] = {1, 5, 15};
  int index = 0;
  for (int i = 0; i < 3; ++i) {
    if (choices[i] == s.idle_lock_minutes) index = i;
  }
  s.idle_lock_minutes = choices[(index + 1) % 3];
  reload();
}

void SettingsPage::on_clipboard_choice(int) {
  SecuritySettings& s = host()->mutable_settings();
  const int choices[3] = {15, 30, 60};
  int index = 0;
  for (int i = 0; i < 3; ++i) {
    if (choices[i] == s.clipboard_clear_seconds) index = i;
  }
  s.clipboard_clear_seconds = choices[(index + 1) % 3];
  reload();
}

void SettingsPage::on_blur_toggle(bool) {
  SecuritySettings& s = host()->mutable_settings();
  s.hide_plaintext_on_blur = !s.hide_plaintext_on_blur;
  reload();
}

void SettingsPage::on_lock_now() {
  host()->lock_now();
  host()->navigate(Page::kSecrets);
}

}  // namespace secretkeeper::ui
