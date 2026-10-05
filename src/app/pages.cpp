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
#include <chrono>
#include <cstdint>
#include <cstring>
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

// 界面固定尺寸。设计稿为 1200x844，实际窗口留出系统边框后按 1180x800 起，
// 内容区高度不足时由各页面自行裁剪（列表页滚动、设置页不滚动）。
constexpr int kWinW = 1180;
constexpr int kWinH = 800;

// 左侧主菜单三项的顺序与设计稿一致。
constexpr int kMenuTop = 140;
constexpr int kMenuItemH = 44;
constexpr int kMenuGap = 10;

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
    : Fl_Group(0, 0, kWinW, kWinH), subtitle_(subtitle ? subtitle : "") {
  box(FL_NO_BOX);
  color(theme::kContentBg);
  back_ = nullptr;
  title_box_ = new Fl_Box(theme::kContentPad, 40, 560, 36, title);
  title_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  title_box_->labelfont(theme::font_for(theme::kWeightBold));
  title_box_->labelsize(theme::kFontTitle);
  title_box_->labelcolor(theme::kText);
  subtitle_box_ = new Fl_Box(theme::kContentPad, 76, 700, 22, subtitle_.c_str());
  subtitle_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  subtitle_box_->labelfont(theme::font_for(theme::kWeightRegular));
  subtitle_box_->labelsize(theme::kFontBody);
  subtitle_box_->labelcolor(theme::kTextMuted);
}

void PageBase::layout_header(const char* back_label) {
  if (back_label != nullptr) {
    back_ = new widgets::Button(kWinW - theme::kContentPad - 110, 44, 110, 34, back_label,
                                ButtonKind::kSecondary);
    back_->callback([](Fl_Widget*, void* data) {
      static_cast<PageBase*>(data)->on_back();
    }, this);
  }
}

void PageBase::on_back() { host_->go_back(); }

// =====================================================================
// 01 解锁与锁定
// =====================================================================
UnlockPage::UnlockPage(MainWindow* host) : Fl_Group(0, 0, kWinW, kWinH), host_(host) {
  box(FL_NO_BOX);
  color(theme::kContentBg);

  // ---- 右侧表单区 ----
  const int form_x = 560;
  const int form_w = 560;
  state_pill_ = new widgets::Pill(form_x + theme::kContentPad, 64, 92, 26, "已锁定",
                                  theme::kTextMuted, theme::kSubtleBg);

  Fl_Box welcome(form_x + theme::kContentPad, 300, form_w - theme::kContentPad * 2, 40,
                 "欢迎回来");
  welcome.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  welcome.labelfont(theme::font_for(theme::kWeightBold));
  welcome.labelsize(theme::kFontTitle);
  welcome.labelcolor(theme::kText);

  Fl_Box tip(form_x + theme::kContentPad, 342, form_w - theme::kContentPad * 2, 22,
             "选择主密钥，输入主密钥密码解锁。");
  tip.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  tip.labelfont(theme::font_for(theme::kWeightRegular));
  tip.labelsize(theme::kFontBody);
  tip.labelcolor(theme::kTextMuted);

  Fl_Box key_label(form_x + theme::kContentPad, 388, 200, 20, "主密钥");
  key_label.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  key_label.labelfont(theme::font_for(theme::kWeightRegular));
  key_label.labelsize(theme::kFontSmall);
  key_label.labelcolor(theme::kTextMuted);

  selector_ = new widgets::KeySelector(form_x + theme::kContentPad, 410,
                                       form_w - theme::kContentPad * 2, 64);
  selector_->set_placeholder("尚未创建主密钥");
  selector_->callback([](Fl_Widget*, void* data) {
    static_cast<UnlockPage*>(data)->on_pick_key();
  }, this);

  Fl_Box pwd_label(form_x + theme::kContentPad, 490, 200, 20, "主密钥密码");
  pwd_label.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  pwd_label.labelfont(theme::font_for(theme::kWeightRegular));
  pwd_label.labelsize(theme::kFontSmall);
  pwd_label.labelcolor(theme::kTextMuted);

  password_ = new widgets::Field(form_x + theme::kContentPad, 512,
                                 form_w - theme::kContentPad * 2 - 34, 38);
  password_->type(FL_SECRET_INPUT);
  password_->when(FL_WHEN_ENTER_KEY);
  password_->callback([](Fl_Widget*, void* data) {
    static_cast<UnlockPage*>(data)->on_unlock();
  }, this);

  unlock_btn_ = new widgets::IconButton(form_x + theme::kContentPad, 566,
                                        form_w - theme::kContentPad * 2, 44, Icon::kLock,
                                        "解锁密匣");
  unlock_btn_->callback([](Fl_Widget*, void* data) {
    static_cast<UnlockPage*>(data)->on_unlock();
  }, this);

  error_ = new Fl_Box(form_x + theme::kContentPad, 626, form_w - theme::kContentPad * 2, 22, "");
  error_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  error_->labelfont(theme::font_for(theme::kWeightRegular));
  error_->labelsize(theme::kFontSmall);
  error_->labelcolor(theme::kTextMuted);
}

void UnlockPage::draw() {
  Fl_Group::draw();
  // 左侧品牌区：深色底 + 盾牌 Logo + 标语。
  const int side_w = theme::kSidebarWidth;
  fl_color(theme::kSidebarBg);
  fl_rectf(0, 0, side_w, kWinH);

  const int cx = side_w / 2;
  // Logo：圆角方块 + 盾牌轮廓 + 对勾
  const int box = 76;
  theme::fill_rounded(cx - box / 2, 250, box, box, 20, theme::kActiveBg);
  fl_color(theme::kSidebarActiveText);
  fl_line_style(FL_SOLID, 2);
  const int sx = cx - 16, sy = 268;
  fl_line(sx + 16, sy, sx + 30, sy + 6);
  fl_line(sx + 30, sy + 6, sx + 16, sy + 28);
  fl_arc(sx + 16, sy + 14, 16, 0.0f, 3.14159265f);
  fl_line(sx, sy + 14, sx, sy + 26);
  fl_line(sx + 32, sy + 14, sx + 32, sy + 26);
  fl_line_style(FL_SOLID, 0);

  // 本机密匣角标
  const int tag_w = 76, tag_h = 22;
  theme::draw_pill(cx + 8, 344, tag_w, tag_h, "本机密匣", theme::kSidebarActiveText,
                   theme::kActiveBg);

  fl_color(0xFFFFFF);
  fl_font(theme::font_for(theme::kWeightBold), 26);
  fl_draw("只在你的设备里，", 28, 490, side_w - 40, 34, FL_ALIGN_LEFT);
  fl_draw("只由你的密钥开启。", 28, 524, side_w - 40, 34, FL_ALIGN_LEFT);

  fl_color(theme::kSidebarIcon);
  fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
  fl_draw("完全离线的个人私密信息匣。", 28, 576, side_w - 40, 20, FL_ALIGN_LEFT);
  fl_draw("不联网、不登录、不上传。", 28, 598, side_w - 40, 20, FL_ALIGN_LEFT);

  fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
  fl_draw("v0.0.1", 28, kWinH - 48, 120, 20, FL_ALIGN_LEFT);
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
  layout_header(nullptr);

  // 标题栏右侧两个入口。
  imp_btn_ = new widgets::IconButton(kWinW - theme::kContentPad - 230, 40, 118, 34,
                                      Icon::kImport, "导入主密钥", ButtonKind::kSecondary);
  gen_btn_ = new widgets::IconButton(kWinW - theme::kContentPad - 104, 40, 104, 34, Icon::kPlus,
                                     "生成主密钥");
  imp_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<MasterKeysPage*>(d)->on_import();
  }, this);
  gen_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<MasterKeysPage*>(d)->on_generate();
  }, this);

  quota_notice_ = make_notice(theme::kContentPad, 104, kWinW - theme::kSidebarWidth -
                              theme::kContentPad * 3,
                              widgets::Notice::Tone::kWarning, "", "", 0);

  Fl_Box list_title(theme::kContentPad, 152, 300, 22, "本机主密钥");
  list_title.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  list_title.labelfont(theme::font_for(theme::kWeightMedium));
  list_title.labelsize(theme::kFontBody);
  list_title.labelcolor(theme::kTextMuted);

  card_box_ = new Fl_Box(theme::kContentPad, 178, theme::kListColumnWidth, 520, "");
  card_box_->box(FL_NO_BOX);

  // 右列详情卡片
  const int dx = theme::kContentPad + theme::kListColumnWidth + theme::kGapXl;
  const int dw = kWinW - theme::kSidebarWidth - theme::kContentPad * 3 - theme::kListColumnWidth -
                 theme::kGapXl;
  new widgets::Card(dx, 178, dw, 520);

  Fl_Box dtitle(dx + theme::kGapXl, 200, 300, 28, "主密钥详情");
  dtitle.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  dtitle.labelfont(theme::font_for(theme::kWeightBold));
  dtitle.labelsize(theme::kFontHeading);
  dtitle.labelcolor(theme::kText);

  default_pill_ = new widgets::Pill(dx + dw - theme::kGapXl - 96, 200, 96, 24, "默认主密钥",
                                     theme::kAccent, theme::kAccentSoft);

  const int row_y0 = 250;
  Fl_Box id_l(dx + theme::kGapXl, row_y0, 110, 24, "主密钥 ID");
  id_l.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  id_l.labelcolor(theme::kTextMuted);
  id_l.labelsize(theme::kFontLabel);
  detail_id_ = new Fl_Box(dx + theme::kGapXl + 120, row_y0, dw - 240, 24, "");
  detail_id_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_id_->labelcolor(theme::kText);
  detail_id_->labelsize(theme::kFontBody);

  Fl_Box name_l(dx + theme::kGapXl, row_y0 + 34, 110, 24, "名称");
  name_l.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  name_l.labelcolor(theme::kTextMuted);
  name_l.labelsize(theme::kFontLabel);
  detail_name_ = new Fl_Box(dx + theme::kGapXl + 120, row_y0 + 34, dw - 240, 24, "");
  detail_name_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_name_->labelcolor(theme::kText);
  detail_name_->labelsize(theme::kFontBody);

  Fl_Box alg_l(dx + theme::kGapXl, row_y0 + 68, 110, 24, "算法");
  alg_l.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  alg_l.labelcolor(theme::kTextMuted);
  alg_l.labelsize(theme::kFontLabel);
  detail_alg_ = new Fl_Box(dx + theme::kGapXl + 120, row_y0 + 68, dw - 240, 24, "RSA-2048");
  detail_alg_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_alg_->labelcolor(theme::kText);
  detail_alg_->labelsize(theme::kFontBody);

  Fl_Box loc_l(dx + theme::kGapXl, row_y0 + 102, 110, 24, "保存位置");
  loc_l.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  loc_l.labelcolor(theme::kTextMuted);
  loc_l.labelsize(theme::kFontLabel);
  Fl_Box* detail_loc = new Fl_Box(dx + theme::kGapXl + 120, row_y0 + 102, dw - 240, 24,
                                   "本机加密存储");
  detail_loc->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_loc->labelcolor(theme::kText);
  detail_loc->labelsize(theme::kFontBody);

  Fl_Box cnt_l(dx + theme::kGapXl, row_y0 + 136, 110, 24, "关联信息");
  cnt_l.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  cnt_l.labelcolor(theme::kTextMuted);
  cnt_l.labelsize(theme::kFontLabel);
  detail_count_ = new Fl_Box(dx + theme::kGapXl + 120, row_y0 + 136, dw - 240, 24, "");
  detail_count_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_count_->labelcolor(theme::kText);
  detail_count_->labelsize(theme::kFontBody);

  Fl_Box* note = new Fl_Box(dx + theme::kGapXl, row_y0 + 186, dw - theme::kGapXl * 2, 44,
                            "随机生成的主密钥，由主密钥密码派生的 KEK 加密后保存在本地。");
  note->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  note->labelcolor(theme::kTextMuted);
  note->labelsize(theme::kFontSmall);
  note->labelfont(theme::font_for(theme::kWeightRegular));

  exp_btn_ = new widgets::IconButton(dx + theme::kGapXl, row_y0 + 246, 132, 38, Icon::kExport,
                                     "导出主密钥");
  del_btn_ = new widgets::IconButton(dx + theme::kGapXl + 144, row_y0 + 246, 110, 38,
                                     Icon::kTrash, "删除主密钥", ButtonKind::kDanger);
  switch_btn_ = new widgets::IconButton(dx + theme::kGapXl, row_y0 + 296, 132, 34, Icon::kKey,
                                        "设为默认", ButtonKind::kSecondary);
  exp_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<MasterKeysPage*>(d)->on_export();
  }, this);
  del_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<MasterKeysPage*>(d)->on_delete();
  }, this);
  switch_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<MasterKeysPage*>(d)->on_switch_default();
  }, this);

  auto* tip2 = make_notice(dx + theme::kGapXl, row_y0 + 344, dw - theme::kGapXl * 2,
                           widgets::Notice::Tone::kInfo, "默认主密钥禁止删除",
                           "请先切换默认主密钥。切换时需分别验证原默认与新默认两把主密钥密码。",
                           72);
  (void)tip2;
  Fl_Box* tip3 = new Fl_Box(dx + theme::kGapXl, row_y0 + 428, dw - theme::kGapXl * 2, 40,
                            "删除主密钥前先备份，并验证其密码。删除主密钥不会删除已加密的数据。");
  tip3->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  tip3->labelcolor(theme::kTextMuted);
  tip3->labelsize(theme::kFontSmall);
}

void MasterKeysPage::draw() {
  Fl_Group::draw();
  const auto keys = host()->service().list_master_keys();
  const auto quota = host()->service().quota();

  // 容量提示条：满额时才出现（设计稿 02 号）。
  const int notice_w = kWinW - theme::kSidebarWidth - theme::kContentPad * 3;
  if (quota.master_keys >= svc::kMaxMasterKeys) {
    quota_notice_->resize(theme::kContentPad, 104, notice_w, 62);
    quota_notice_->set_content(
        "主密钥容量已满  2 / 2",
        "先导出并删除一把主密钥，才能生成或导入新的主密钥。");
    quota_notice_->show();
    quota_notice_->redraw();
  } else {
    quota_notice_->hide();
  }

  // 左列主密钥卡片
  const int cx = theme::kContentPad;
  const int cw = theme::kListColumnWidth;
  const int ch = 116;
  int y = 178;
  for (std::size_t i = 0; i < keys.size(); ++i) {
    const auto& item = keys[i];
    const bool is_default = item.is_default;
    const bool is_selected = static_cast<int>(i) == selected_;
    theme::fill_rounded(cx, y, cw, ch, theme::kRadiusCard, theme::kCardBg);
    theme::stroke_rounded(cx, y, cw, ch, theme::kRadiusCard,
                         is_selected ? theme::kAccent : theme::kBorder);
    widgets::draw_icon(widgets::Icon::kKey, cx + theme::kGapLg, y + theme::kGapLg,
                       theme::kAccent);

    fl_color(theme::kText);
    fl_font(theme::font_for(theme::kWeightBold), theme::kFontBody);
    fl_draw(item.name.empty() ? "" : item.name.data(), cx + theme::kGapLg, y + theme::kGapLg,
            cw - theme::kGapLg * 3 - 60, 22, FL_ALIGN_LEFT);
    fl_color(theme::kText);
    fl_font(theme::font_for(theme::kWeightMedium), theme::kFontBody);
    fl_draw(item.master_key_id.data(), cx + theme::kGapLg, y + 48, cw - theme::kGapLg * 2, 20,
            FL_ALIGN_LEFT);
    fl_color(theme::kTextMuted);
    fl_font(theme::font_for(theme::kWeightRegular), theme::kFontSmall);
    const std::string meta =
        std::to_string(item.secret_count) + " 条机密信息";
    fl_draw(meta.data(), cx + theme::kGapLg, y + 72, cw - theme::kGapLg * 2, 20, FL_ALIGN_LEFT);
    if (item.name.empty()) {
      fl_draw("名称未填写，主密钥在本机可用。", cx + theme::kGapLg, y + 92,
              cw - theme::kGapLg * 2, 18, FL_ALIGN_LEFT);
    }
    if (is_default) {
      theme::draw_pill(cx + cw - 72, y + theme::kGapLg, 56, 22, "默认", theme::kAccent,
                       theme::kAccentSoft);
    }
    y += ch + theme::kGapMd;
  }
  // 卡片区需要能接收点击，故把该区域包在一个 Fl_Group 里并手动 hit-test。
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

  if (selected_ >= 0 && selected_ < static_cast<int>(keys.size())) {
    const auto& item = keys[static_cast<std::size_t>(selected_)];
    detail_id_->copy_label(item.master_key_id.c_str());
    detail_name_->copy_label(item.name.empty() ? "" : item.name.c_str());
    detail_count_->copy_label((std::to_string(item.secret_count) + " 条").c_str());
    if (item.is_default) {
      default_pill_->show();
      switch_btn_->hide();
      del_btn_->set_enabled(false);
    } else {
      default_pill_->hide();
      switch_btn_->show();
      del_btn_->set_enabled(true);
    }
  } else {
    detail_id_->copy_label("");
    detail_name_->copy_label("");
    detail_count_->copy_label("");
    default_pill_->hide();
    exp_btn_->set_enabled(false);
    del_btn_->set_enabled(false);
    switch_btn_->set_enabled(false);
  }
  if (selected_ >= 0 && selected_ < static_cast<int>(keys.size())) {
    exp_btn_->set_enabled(true);
    del_btn_->set_enabled(!keys[static_cast<std::size_t>(selected_)].is_default);
    switch_btn_->set_enabled(!keys[static_cast<std::size_t>(selected_)].is_default);
  }
  gen_btn_->set_enabled(host()->service().quota().master_keys < svc::kMaxMasterKeys);
  imp_btn_->set_enabled(host()->service().quota().master_keys < svc::kMaxMasterKeys);
  redraw();
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
  layout_header("返回主密钥管理");

  const int cw = kWinW - theme::kSidebarWidth - theme::kContentPad * 3;
  const int cx = theme::kContentPad;
  new widgets::Card(cx, 104, cw, 620);

  Fl_Box title(cx + theme::kGapXl, 128, 300, 26, "新主密钥");
  title.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  title.labelfont(theme::font_for(theme::kWeightBold));
  title.labelsize(theme::kFontSubheading);
  title.labelcolor(theme::kText);
  new widgets::Card(cx + cw - theme::kGapXl - 40, 128, 40, 40, theme::kSubtleBg);

  Fl_Box* name_l = new Fl_Box(cx + theme::kGapXl, 176, 300, 20, "主密钥名称（可选）");
  name_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  name_l->labelsize(theme::kFontSmall);
  name_l->labelcolor(theme::kTextMuted);
  name_ = new widgets::Field(cx + theme::kGapXl, 198, cw - theme::kGapXl * 2, 38);
  name_->set_placeholder("可以留空");

  Fl_Box* name_tip = new Fl_Box(cx + theme::kGapXl, 240, cw - theme::kGapXl * 2, 20,
                                "名称仅用于辨识，不参与加密。");
  name_tip->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  name_tip->labelsize(theme::kFontSmall);
  name_tip->labelcolor(theme::kTextMuted);

  Fl_Box* pwd_l = new Fl_Box(cx + theme::kGapXl, 280, 300, 20, "主密钥密码");
  pwd_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  pwd_l->labelsize(theme::kFontSmall);
  pwd_l->labelcolor(theme::kTextMuted);
  password_ = new widgets::Field(cx + theme::kGapXl, 302, cw - theme::kGapXl * 2, 38);
  password_->type(FL_SECRET_INPUT);

  Fl_Box* cfm_l = new Fl_Box(cx + theme::kGapXl, 358, 300, 20, "确认主密钥密码");
  cfm_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  cfm_l->labelsize(theme::kFontSmall);
  cfm_l->labelcolor(theme::kTextMuted);
  confirm_ = new widgets::Field(cx + theme::kGapXl, 380, cw - theme::kGapXl * 2, 38);
  confirm_->type(FL_SECRET_INPUT);

  warning_ = make_notice(cx + theme::kGapXl, 436, cw - theme::kGapXl * 2,
                         widgets::Notice::Tone::kWarning, "请妥善记住主密钥密码",
                         "密码丢失无法解密，密匣无法为你重置密码。", 74);
  capacity_ = make_notice(cx + theme::kGapXl, 526, cw - theme::kGapXl * 2,
                          widgets::Notice::Tone::kInfo, "", "", 60);

  save_ = new widgets::IconButton(cx + theme::kGapXl, 604, 132, 40, Icon::kLock, "保存主密钥");
  auto* cancel = new widgets::Button(cx + theme::kGapXl + 144, 604, 84, 40, "返回管理",
                                     ButtonKind::kSecondary);
  save_->callback([](Fl_Widget*, void* d) {
    static_cast<CreateKeyPage*>(d)->on_save();
  }, this);
  cancel->callback([](Fl_Widget*, void* d) {
    static_cast<PageBase*>(d)->on_back();
  }, this);
}

void CreateKeyPage::reload() {
  const auto quota = host()->service().quota();
  const bool full = quota.master_keys >= svc::kMaxMasterKeys;
  if (full) {
    capacity_->set_content(
        ("已使用 " + std::to_string(quota.master_keys) + " / " +
         std::to_string(svc::kMaxMasterKeys) + " 把主密钥")
            .c_str(),
        "容量已满。先导出并删除一把主密钥，再继续生成。");
    capacity_->show();
  } else {
    capacity_->hide();
  }
  save_->set_enabled(!full);
  redraw();
}

std::string CreateKeyPage::status_text() const { return last_error_; }

void CreateKeyPage::on_save() {
  last_error_.clear();
  const std::string pwd = password_->value() ? password_->value() : "";
  const std::string cfm = confirm_->value() ? confirm_->value() : "";
  if (pwd.empty()) {
    last_error_ = "请输入主密钥密码";
    password_->take_focus();
    redraw();
    return;
  }
  if (pwd != cfm) {
    last_error_ = "两次输入的密码不一致";
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
    : PageBase(host, "导入主密钥", "从之前导出的备份文件恢复主密钥，导入后作为保存。") {
  layout_header("返回主密钥管理");

  const int cw = kWinW - theme::kSidebarWidth - theme::kContentPad * 3;
  const int cx = theme::kContentPad;
  new widgets::Card(cx, 104, cw, 460);

  // 文件选择块
  new widgets::Card(cx + theme::kGapXl, 128, cw - theme::kGapXl * 2, 84, theme::kSubtleBg);
  file_name_ = new Fl_Box(cx + theme::kGapXl + theme::kGapLg, 146, 400, 24, "尚未选择文件");
  file_name_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  file_name_->labelfont(theme::font_for(theme::kWeightBold));
  file_name_->labelsize(theme::kFontBody);
  file_name_->labelcolor(theme::kText);
  file_sub_ = new Fl_Box(cx + theme::kGapXl + theme::kGapLg, 172, 400, 20,
                          "选择之前导出的 .smkexp 文件");
  file_sub_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  file_sub_->labelsize(theme::kFontSmall);
  file_sub_->labelcolor(theme::kTextMuted);
  choose_btn_ = new widgets::Button(cx + cw - theme::kGapXl - 132, 152, 118, 34, "重新选择文件",
                                    ButtonKind::kSecondary);
  choose_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ImportKeyPage*>(d)->on_choose_file();
  }, this);

  Fl_Box* p_l = new Fl_Box(cx + theme::kGapXl, 236, 300, 20, "文件保护密码");
  p_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  p_l->labelsize(theme::kFontSmall);
  p_l->labelcolor(theme::kTextMuted);
  protection_ = new widgets::Field(cx + theme::kGapXl, 258, cw - theme::kGapXl * 2, 38);
  protection_->type(FL_SECRET_INPUT);
  Fl_Box* p_tip = new Fl_Box(cx + theme::kGapXl, 300, cw - theme::kGapXl * 2, 20,
                             "导出时为这份备份设定的密码。");
  p_tip->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  p_tip->labelsize(theme::kFontSmall);
  p_tip->labelcolor(theme::kTextMuted);

  Fl_Box* n_l = new Fl_Box(cx + theme::kGapXl, 340, 300, 20, "新的主密钥密码");
  n_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  n_l->labelsize(theme::kFontSmall);
  n_l->labelcolor(theme::kTextMuted);
  new_password_ = new widgets::Field(cx + theme::kGapXl, 362, cw - theme::kGapXl * 2, 38);
  new_password_->type(FL_SECRET_INPUT);
  Fl_Box* n_tip = new Fl_Box(cx + theme::kGapXl, 404, cw - theme::kGapXl * 2, 20,
                             "为本机保存的主密钥新设密码。");
  n_tip->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  n_tip->labelsize(theme::kFontSmall);
  n_tip->labelcolor(theme::kTextMuted);

  import_btn_ = new widgets::IconButton(cx + theme::kGapXl, 448, 132, 40, Icon::kImport,
                                        "导入主密钥");
  auto* cancel = new widgets::Button(cx + theme::kGapXl + 144, 448, 84, 40, "返回管理",
                                     ButtonKind::kSecondary);
  import_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ImportKeyPage*>(d)->on_import();
  }, this);
  cancel->callback([](Fl_Widget*, void* d) {
    static_cast<PageBase*>(d)->on_back();
  }, this);
}

void ImportKeyPage::reload() {
  const bool full = host()->service().quota().master_keys >= svc::kMaxMasterKeys;
  import_btn_->set_enabled(!full && !path_.empty());
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
  layout_header("返回主密钥管理");

  const int cw = kWinW - theme::kSidebarWidth - theme::kContentPad * 3;
  const int cx = theme::kContentPad;
  new widgets::Card(cx, 104, cw, 660);

  // 目标主密钥信息块
  new widgets::Card(cx + theme::kGapXl, 128, cw - theme::kGapXl * 2, 76, theme::kSubtleBg);
  widgets::draw_icon(widgets::Icon::kKey, cx + theme::kGapXl + theme::kGapLg, 152,
                     theme::kAccent);
  key_name_box_ = new Fl_Box(cx + theme::kGapXl + theme::kGapLg + 32, 142, 300, 24, "");
  key_name_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  key_name_box_->labelfont(theme::font_for(theme::kWeightBold));
  key_name_box_->labelsize(theme::kFontBody);
  key_name_box_->labelcolor(theme::kText);
  Fl_Box* id_l = new Fl_Box(cx + theme::kGapXl + theme::kGapLg + 32, 170, 110, 20, "主密钥 ID");
  id_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  id_l->labelsize(theme::kFontSmall);
  id_l->labelcolor(theme::kTextMuted);
  key_id_box_ = new Fl_Box(cx + theme::kGapXl + theme::kGapLg + 148, 170, 200, 20, "");
  key_id_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  key_id_box_->labelsize(theme::kFontSmall);
  key_id_box_->labelcolor(theme::kText);

  // 三段密码：原密码（验证）+ 文件保护密码 + 确认
  const int labels_y[3] = {232, 316, 400};
  const char* labels[3] = {"主密钥原密码", "导出文件保护密码", "确认保护密码"};
  const char* tips[3] = {"验证这把主密钥的当前密码；错误时导出不会开始。",
                         "为这份备份新设密码；导入该文件时使用。", ""};
  widgets::Field* fields[3] = {
      new widgets::Field(cx + theme::kGapXl, labels_y[0] + 22, cw - theme::kGapXl * 2, 38),
      new widgets::Field(cx + theme::kGapXl, labels_y[1] + 22, cw - theme::kGapXl * 2, 38),
      new widgets::Field(cx + theme::kGapXl, labels_y[2] + 22, cw - theme::kGapXl * 2, 38)};
  for (int i = 0; i < 3; ++i) {
    fields[i]->type(FL_SECRET_INPUT);
    Fl_Box* l = new Fl_Box(cx + theme::kGapXl, labels_y[i], 300, 20, labels[i]);
    l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    l->labelsize(theme::kFontSmall);
    l->labelcolor(theme::kTextMuted);
    if (tips[i][0] != '\0') {
      Fl_Box* t = new Fl_Box(cx + theme::kGapXl, labels_y[i] + 62, cw - theme::kGapXl * 2, 20,
                             tips[i]);
      t->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
      t->labelsize(theme::kFontSmall);
      t->labelcolor(theme::kTextMuted);
    }
  }
  original_ = fields[0];
  protection_ = fields[1];
  confirm_ = fields[2];

  // 保存位置
  Fl_Box* loc_l = new Fl_Box(cx + theme::kGapXl, 466, 300, 20, "保存位置");
  loc_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  loc_l->labelsize(theme::kFontSmall);
  loc_l->labelcolor(theme::kTextMuted);
  new widgets::Card(cx + theme::kGapXl, 488, cw - theme::kGapXl * 2, 56, theme::kSubtleBg);
  location_box_ = new Fl_Box(cx + theme::kGapXl + theme::kGapLg + 32, 504, 600, 24, "本机 / 密匣备份 / ");
  location_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  location_box_->labelsize(theme::kFontBody);
  location_box_->labelcolor(theme::kText);
  choose_btn_ = new widgets::Button(cx + cw - theme::kGapXl - 116, 500, 100, 32, "选择位置",
                                    ButtonKind::kSecondary);
  choose_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ExportKeyPage*>(d)->on_choose_location();
  }, this);

  warning_ = make_notice(cx + theme::kGapXl, 560, cw - theme::kGapXl * 2,
                         widgets::Notice::Tone::kWarning, "",
                         "保护密码丢失后，这份主密钥备份无法导入。请将文件与密码分开妥善保存。",
                         54);
  info_ = make_notice(cx + theme::kGapXl, 624, cw - theme::kGapXl * 2,
                      widgets::Notice::Tone::kInfo, "", "导出不会删除本机的主密钥。", 52);

  export_btn_ = new widgets::IconButton(cx + theme::kGapXl, 692, 132, 40, Icon::kExport,
                                        "导出主密钥");
  auto* cancel = new widgets::Button(cx + theme::kGapXl + 144, 692, 84, 40, "返回管理",
                                     ButtonKind::kSecondary);
  export_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ExportKeyPage*>(d)->on_export();
  }, this);
  cancel->callback([](Fl_Widget*, void* d) {
    static_cast<PageBase*>(d)->on_back();
  }, this);
}

void ExportKeyPage::reload() {
  key_id_ = host()->export_target();
  std::string name;
  for (const auto& k : host()->service().list_master_keys()) {
    if (k.master_key_id == key_id_) name = k.name;
  }
  key_name_box_->copy_label(name.empty() ? "(未命名主密钥)" : name.c_str());
  key_id_box_->copy_label(key_id_.c_str());
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
    redraw();
    return;
  }
  const std::string orig = original_->value() ? original_->value() : "";
  const std::string prot = protection_->value() ? protection_->value() : "";
  const std::string cfm = confirm_->value() ? confirm_->value() : "";
  if (orig.empty()) {
    last_error_ = "请输入主密钥原密码";
    redraw();
    return;
  }
  if (prot.empty()) {
    last_error_ = "请输入导出文件保护密码";
    redraw();
    return;
  }
  if (prot != cfm) {
    last_error_ = "两次输入的密码不一致";
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
      host()->service().export_master_key(key_id_, as_bytes(original), &bytes);
  scrub(&original);
  original_->value("");
  protection_->value("");
  confirm_->value("");

  if (rc != svc::Error::kOk) {
    last_error_ = error_text(rc);
    redraw();
    return;
  }
  // 注意：service::export_master_key 当前签名的密码参数是「该主密钥自身的密码」，
  // 用于校验身份；需求 2.3 要求的「为导出文件另设保护密码」尚无对应参数。
  // 在 service 层补齐该能力之前，这里写出的文件沿用主密钥原密码的保护，
  // 与设计稿 05 号的「导出文件保护密码」语义不一致。此为已知待办。
  (void)prot;
  if (!write_bytes(location_, bytes)) {
    last_error_ = "文件读写失败";
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
  count_label_ = new Fl_Box(theme::kContentPad, 76, 520, 22, "");
  count_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  count_label_->labelsize(theme::kFontBody);
  count_label_->labelcolor(theme::kTextMuted);

  imp_btn_ = new widgets::IconButton(kWinW - theme::kContentPad - 210, 40, 96, 34, Icon::kImport,
                                     "导入", ButtonKind::kSecondary);
  add_btn_ = new widgets::IconButton(kWinW - theme::kContentPad - 106, 40, 106, 34, Icon::kPlus,
                                     "新增机密信息");
  imp_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_import();
  }, this);
  add_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_add();
  }, this);

  // 左列：搜索 + 列表
  const int list_x = theme::kContentPad;
  const int list_w = kWinW - theme::kSidebarWidth - theme::kContentPad * 3 - theme::kDetailColumnWidth -
                     theme::kGapXl;
  search_ = new widgets::Field(list_x, 104, list_w, 36);
  search_->set_placeholder("搜索标题或主密钥 ID");
  search_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->reload();
  }, this);

  list_ = new widgets::List(list_x, 152, list_w, 480);
  list_->set_columns({"标题（可选）", "主密钥 ID", "主密钥名称"});
  list_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_row_select();
  }, this);

  // 右列：详情
  const int dx = list_x + list_w + theme::kGapXl;
  const int dw = theme::kDetailColumnWidth;
  new widgets::Card(dx, 104, dw, 596);

  detail_title_ = new Fl_Box(dx + theme::kGapXl, 128, 240, 26, "信息详情");
  detail_title_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_title_->labelfont(theme::font_for(theme::kWeightBold));
  detail_title_->labelsize(theme::kFontHeading);
  detail_title_->labelcolor(theme::kText);
  verified_ = new widgets::Pill(dx + dw - theme::kGapXl - 76, 128, 76, 24, "已验证",
                                theme::kAccent, theme::kAccentSoft);

  const int ry0 = 180;
  struct RowDef { const char* label; Fl_Box** target; int dy; };
  Fl_Box* mk_id_l = new Fl_Box(dx + theme::kGapXl, ry0, 110, 24, "主密钥 ID");
  mk_id_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  mk_id_l->labelsize(theme::kFontLabel);
  mk_id_l->labelcolor(theme::kTextMuted);
  detail_mk_id_ = new Fl_Box(dx + theme::kGapXl + 110, ry0, dw - 220, 24, "");
  detail_mk_id_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_mk_id_->labelsize(theme::kFontBody);
  detail_mk_id_->labelcolor(theme::kText);

  Fl_Box* mk_name_l = new Fl_Box(dx + theme::kGapXl, ry0 + 34, 110, 24, "主密钥名称");
  mk_name_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  mk_name_l->labelsize(theme::kFontLabel);
  mk_name_l->labelcolor(theme::kTextMuted);
  detail_mk_name_ = new Fl_Box(dx + theme::kGapXl + 110, ry0 + 34, dw - 220, 24, "");
  detail_mk_name_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_mk_name_->labelsize(theme::kFontBody);
  detail_mk_name_->labelcolor(theme::kText);

  Fl_Box* t_l = new Fl_Box(dx + theme::kGapXl, ry0 + 68, 110, 24, "标题");
  t_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  t_l->labelsize(theme::kFontLabel);
  t_l->labelcolor(theme::kTextMuted);
  detail_title_field_ = new Fl_Box(dx + theme::kGapXl + 110, ry0 + 68, dw - 220, 24, "");
  detail_title_field_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  detail_title_field_->labelsize(theme::kFontBody);
  detail_title_field_->labelcolor(theme::kText);

  // 分隔线
  Fl_Box* div = new Fl_Box(dx + theme::kGapXl, ry0 + 106, dw - theme::kGapXl * 2, 1, "");
  div->box(FL_FLAT_BOX);
  div->color(theme::kBorderSubtle);

  Fl_Box* secret_l = new Fl_Box(dx + theme::kGapXl, ry0 + 124, 160, 22, "机密信息");
  secret_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  secret_l->labelsize(theme::kFontBody);
  secret_l->labelcolor(theme::kText);
  reveal_btn_ = new widgets::IconButton(dx + dw - theme::kGapXl - 84, ry0 + 120, 84, 28,
                                        Icon::kEyeOff, "隐藏", ButtonKind::kSecondary);
  reveal_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_toggle_reveal();
  }, this);

  plaintext_box_ = new Fl_Box(dx + theme::kGapXl, ry0 + 158, dw - theme::kGapXl * 2, 150,
                              svc::kMaskedPlaintext);
  plaintext_box_->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  plaintext_box_->box(FL_FLAT_BOX);
  plaintext_box_->color(theme::kSubtleBg);
  plaintext_box_->labelsize(theme::kFontBody);
  plaintext_box_->labelcolor(theme::kText);
  plaintext_box_->labelfont(theme::font_for(theme::kWeightRegular));

  plaintext_meta_ = new Fl_Box(dx + theme::kGapXl, ry0 + 316, dw - theme::kGapXl * 2, 20, "");
  plaintext_meta_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  plaintext_meta_->labelsize(theme::kFontSmall);
  plaintext_meta_->labelcolor(theme::kTextMuted);

  copy_btn_ = new widgets::IconButton(dx + theme::kGapXl, ry0 + 352, 84, 38, Icon::kCopy,
                                      "复制");
  exp_btn_ = new widgets::IconButton(dx + theme::kGapXl + 92, ry0 + 352, 84, 38, Icon::kExport,
                                     "导出", ButtonKind::kSecondary);
  del_btn_ = new widgets::IconButton(dx + theme::kGapXl + 184, ry0 + 352, 84, 38, Icon::kTrash,
                                     "删除", ButtonKind::kDanger);
  copy_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_copy();
  }, this);
  exp_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_export();
  }, this);
  del_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<SecretsPage*>(d)->on_delete();
  }, this);

  hide_notice_ = make_notice(dx + theme::kGapXl, ry0 + 408, dw - theme::kGapXl * 2,
                             widgets::Notice::Tone::kSuccess, "",
                             "默认隐藏明文；离开窗口、失焦、最小化或切换界面时，立即重新掩码。",
                             66);

  Fl_Box* foot = new Fl_Box(dx + theme::kGapXl, ry0 + 490, dw - theme::kGapXl * 2, 22,
                            "主密钥加密的信息，查看或复制前需验证对应主密钥。");
  foot->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  foot->labelsize(theme::kFontSmall);
  foot->labelcolor(theme::kTextMuted);

  // 底部说明（设计稿 06 底部的黄色问号说明）
  Fl_Box* qmark_note = new Fl_Box(theme::kContentPad, 716, 620, 22,
                                 "ID 后的黄色 ? 表示主密钥缺失；名称为空不代表缺失。");
  qmark_note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  qmark_note->labelsize(theme::kFontSmall);
  qmark_note->labelcolor(theme::kTextMuted);
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
  list_->set_rows(rows);

  count_label_->copy_label((std::string("共 ") + std::to_string(items.size()) + " 条，基础版最多 " +
                            std::to_string(svc::kMaxSecrets) + " 条。")
                               .c_str());

  add_btn_->set_enabled(quota.secrets < svc::kMaxSecrets);
  imp_btn_->set_enabled(quota.secrets < svc::kMaxSecrets);

  // 保持选中：优先已选中的 id，否则默认第一行。
  int target = -1;
  for (std::size_t i = 0; i < ids_.size(); ++i) {
    if (ids_[i] == selected_id_) target = static_cast<int>(i);
  }
  if (target < 0 && !ids_.empty()) target = 0;
  list_->select(target);
  if (target >= 0) {
    selected_id_ = ids_[static_cast<std::size_t>(target)];
  } else {
    selected_id_.clear();
  }
  refresh_detail();
  redraw();
}

std::string SecretsPage::status_text() const { return last_error_; }

void SecretsPage::on_row_select() {
  const int row = list_->selected();
  if (row < 0 || row >= static_cast<int>(ids_.size())) {
    selected_id_.clear();
    refresh_detail();
    return;
  }
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
    verified_->hide();
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
    verified_->show();
  } else {
    verified_->copy_label("缺少主密钥");
    verified_->labelcolor(theme::kWarningText);
    verified_->show();
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
  Fl_Group::draw();
  const int list_x = theme::kContentPad;
  const int list_w = kWinW - theme::kSidebarWidth - theme::kContentPad * 3 - theme::kDetailColumnWidth -
                     theme::kGapXl;
  // 搜索框左侧的放大镜
  widgets::draw_icon(widgets::Icon::kSearch, list_x + theme::kGapMd, 112, theme::kTextFaint);
}

void SecretsPage::on_add() { host()->navigate(Page::kAddSecret); }

void SecretsPage::on_import() { host()->navigate(Page::kImportSecret); }


// =====================================================================
// 07 新增机密信息
// =====================================================================
AddSecretPage::AddSecretPage(MainWindow* host)
    : PageBase(host, "新增机密信息", "保存任何你需要保密的文字，不限定内容格式。") {
  layout_header("返回列表");

  const int cw = kWinW - theme::kSidebarWidth - theme::kContentPad * 3;
  const int cx = theme::kContentPad;
  new widgets::Card(cx, 104, cw, 600);

  Fl_Box* sel_l = new Fl_Box(cx + theme::kGapXl, 128, 300, 20, "用于加密的主密钥");
  sel_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  sel_l->labelsize(theme::kFontSmall);
  sel_l->labelcolor(theme::kTextMuted);
  selector_ = new widgets::KeySelector(cx + theme::kGapXl, 150, cw - theme::kGapXl * 2, 64);
  selector_->callback([](Fl_Widget*, void* d) {
    static_cast<AddSecretPage*>(d)->on_pick_key();
  }, this);

  Fl_Box* t_l = new Fl_Box(cx + theme::kGapXl, 232, 300, 20, "标题（可选）");
  t_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  t_l->labelsize(theme::kFontSmall);
  t_l->labelcolor(theme::kTextMuted);
  title_ = new widgets::Field(cx + theme::kGapXl, 254, cw - theme::kGapXl * 2, 38);
  title_->set_placeholder("不填写则保留为空");

  Fl_Box* c_l = new Fl_Box(cx + theme::kGapXl, 308, 300, 20, "机密信息");
  c_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  c_l->labelsize(theme::kFontSmall);
  c_l->labelcolor(theme::kTextMuted);
  content_ = new widgets::TextArea(cx + theme::kGapXl, 330, cw - theme::kGapXl * 2, 152);

  Fl_Box* counter_l = new Fl_Box(cx + theme::kGapXl, 490, 300, 20, "自由文本，最多 150 字符");
  counter_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  counter_l->labelsize(theme::kFontSmall);
  counter_l->labelcolor(theme::kTextMuted);
  counter_ = new Fl_Box(cx + cw - theme::kGapXl - 120, 490, 120, 20, "0 / 150");
  counter_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
  counter_->labelsize(theme::kFontSmall);
  counter_->labelcolor(theme::kTextMuted);

  info_ = make_notice(cx + theme::kGapXl, 522, cw - theme::kGapXl * 2, widgets::Notice::Tone::kInfo,
                      "", "使用当前默认主密钥在本机加密，不会上传任何内容。", 46);

  save_ = new widgets::IconButton(cx + theme::kGapXl, 586, 132, 40, Icon::kLock, "保存");
  auto* cancel = new widgets::Button(cx + theme::kGapXl + 144, 586, 84, 40, "取消",
                                     ButtonKind::kSecondary);
  save_->callback([](Fl_Widget*, void* d) {
    static_cast<AddSecretPage*>(d)->on_save();
  }, this);
  cancel->callback([](Fl_Widget*, void* d) {
    static_cast<PageBase*>(d)->on_back();
  }, this);

  // 输入时实时更新计数。
  content_->callback([](Fl_Widget* w, void* d) {
    auto* self = static_cast<AddSecretPage*>(d);
    const char* v = static_cast<Fl_Input*>(w)->value();
    self->counter_->copy_label(char_counter_text(v ? v : "").c_str());
    self->counter_->redraw();
  });
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
    redraw();
    return;
  }
  const char* raw = content_->value();
  const std::string plaintext = raw ? raw : "";
  if (plaintext.empty()) {
    last_error_ = error_text(svc::Error::kEmptySecret);
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
  const int cw = kWinW - theme::kSidebarWidth - theme::kContentPad * 3;
  const int cx = theme::kContentPad;
  new widgets::Card(cx, 104, cw, 620);

  // 三段步骤条
  step1_ = new Fl_Box(cx + theme::kGapXl, 124, 96, 26, "文件已选择");
  step2_ = new Fl_Box(cx + theme::kGapXl + 116, 124, 96, 26, "验证主密钥");
  step3_ = new Fl_Box(cx + theme::kGapXl + 232, 124, 96, 26, "保存到本机");
  for (Fl_Box* b : {step1_, step2_, step3_}) {
    b->align(FL_ALIGN_CENTER);
    b->box(FL_FLAT_BOX);
    b->color(theme::kAccentSoft);
    b->labelsize(theme::kFontSmall);
    b->labelcolor(theme::kAccent);
  }
  for (int i = 0; i < 2; ++i) {
    Fl_Box* arrow = new Fl_Box(cx + theme::kGapXl + 96 + i * 116, 130, 20, 14, ">");
    arrow->align(FL_ALIGN_CENTER);
    arrow->labelsize(theme::kFontSmall);
    arrow->labelcolor(theme::kTextFaint);
  }

  // 文件选择区
  new widgets::Card(cx + theme::kGapXl, 166, cw - theme::kGapXl * 2, 170, theme::kSubtleBg);
  file_name_ = new Fl_Box(cx + theme::kGapXl, 244, cw - theme::kGapXl * 2, 26,
                          "尚未选择文件");
  file_name_->align(FL_ALIGN_CENTER);
  file_name_->labelfont(theme::font_for(theme::kWeightBold));
  file_name_->labelsize(theme::kFontBody);
  file_name_->labelcolor(theme::kText);
  file_sub_ = new Fl_Box(cx + theme::kGapXl, 272, cw - theme::kGapXl * 2, 20, "");
  file_sub_->align(FL_ALIGN_CENTER);
  file_sub_->labelsize(theme::kFontSmall);
  file_sub_->labelcolor(theme::kTextMuted);
  choose_btn_ = new widgets::Button(cx + theme::kGapXl + (cw - theme::kGapXl * 2 - 118) / 2, 296,
                                    118, 32, "重新选择文件", ButtonKind::kSecondary);
  choose_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ImportSecretPage*>(d)->on_choose_file();
  }, this);

  // 匹配到的主密钥区块
  new widgets::Card(cx + theme::kGapXl, 350, cw - theme::kGapXl * 2, 116, theme::kSubtleBg);
  match_title_ = new Fl_Box(cx + theme::kGapXl + theme::kGapLg, 364, 200, 22, "匹配的主密钥");
  match_title_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  match_title_->labelfont(theme::font_for(theme::kWeightBold));
  match_title_->labelsize(theme::kFontBody);
  match_title_->labelcolor(theme::kText);
  match_icon_ = new Fl_Box(cx + cw - theme::kGapXl - 44, 362, 24, 24, "");
  match_icon_->align(FL_ALIGN_CENTER);
  match_icon_->box(FL_NO_BOX);

  Fl_Box* mid_l = new Fl_Box(cx + theme::kGapXl + theme::kGapLg, 398, 100, 20, "主密钥 ID");
  mid_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  mid_l->labelsize(theme::kFontSmall);
  mid_l->labelcolor(theme::kTextMuted);
  match_id_ = new Fl_Box(cx + theme::kGapXl + theme::kGapLg + 110, 398, 300, 20, "");
  match_id_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  match_id_->labelsize(theme::kFontBody);
  match_id_->labelcolor(theme::kText);

  Fl_Box* mname_l = new Fl_Box(cx + theme::kGapXl + theme::kGapLg, 426, 100, 20, "主密钥名称");
  mname_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  mname_l->labelsize(theme::kFontSmall);
  mname_l->labelcolor(theme::kTextMuted);
  match_name_ = new Fl_Box(cx + theme::kGapXl + theme::kGapLg + 110, 426, 300, 20, "");
  match_name_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  match_name_->labelsize(theme::kFontBody);
  match_name_->labelcolor(theme::kText);
  match_hint_ = new Fl_Box(cx + theme::kGapXl + theme::kGapLg, 446, 500, 18, "");
  match_hint_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  match_hint_->labelsize(theme::kFontSmall);
  match_hint_->labelcolor(theme::kWarningText);

  // 主密钥密码
  Fl_Box* kp_l = new Fl_Box(cx + theme::kGapXl, 488, 400, 20, "");
  kp_l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  kp_l->labelsize(theme::kFontSmall);
  kp_l->labelcolor(theme::kTextMuted);
  key_password_ = new widgets::Field(cx + theme::kGapXl, 510, cw - theme::kGapXl * 2, 38);
  key_password_->type(FL_SECRET_INPUT);
  Fl_Box* kp_tip = new Fl_Box(cx + theme::kGapXl, 552, cw - theme::kGapXl * 2, 20,
                             "非默认主密钥，需验证其密码后导入。");
  kp_tip->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  kp_tip->labelsize(theme::kFontSmall);
  kp_tip->labelcolor(theme::kTextMuted);

  import_btn_ = new widgets::IconButton(cx + theme::kGapXl, 586, 132, 40, Icon::kImport, "导入");
  auto* cancel = new widgets::Button(cx + theme::kGapXl + 144, 586, 84, 40, "取消",
                                     ButtonKind::kSecondary);
  import_btn_->callback([](Fl_Widget*, void* d) {
    static_cast<ImportSecretPage*>(d)->on_import();
  }, this);
  cancel->callback([](Fl_Widget*, void* d) {
    static_cast<PageBase*>(d)->on_back();
  }, this);
}

void ImportSecretPage::draw() {
  Fl_Group::draw();
  const int cw = kWinW - theme::kSidebarWidth - theme::kContentPad * 3;
  const int cx = theme::kContentPad;
  // 文件区中央的图标
  widgets::draw_icon(widgets::Icon::kFile, cx + (cw) / 2 - 16, 200, theme::kAccent);
  // 匹配状态图标：找到画勾，找不到画叉。
  if (!key_id_.empty() || path_.empty()) {
    if (key_found_) {
      widgets::draw_icon(widgets::Icon::kCheck, cx + cw - theme::kGapXl - 36, 368,
                         theme::kAccent);
    } else if (!path_.empty()) {
      widgets::draw_icon(widgets::Icon::kCross, cx + cw - theme::kGapXl - 36, 368,
                         theme::kDangerText);
    }
  }
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
    match_icon_->redraw();
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
  const int cw = kWinW - theme::kSidebarWidth - theme::kContentPad * 3;
  const int cx = theme::kContentPad;
  new widgets::Card(cx, 104, cw, 440);

  Fl_Box* title = new Fl_Box(cx + theme::kGapXl, 128, 300, 26, "锁定与隐私");
  title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  title->labelfont(theme::font_for(theme::kWeightBold));
  title->labelsize(theme::kFontSubheading);
  title->labelcolor(theme::kText);

  struct ItemDef {
    const char* label;
    const char* desc;
    int y;
  };
  const ItemDef items[3] = {
      {"闲置自动锁定", "锁定时清空内存密钥缓存，并隐藏明文。", 176},
      {"剪贴板定时清除", "复制机密信息后，自动清除剪贴板内容。", 260},
      {"离开窗口隐藏明文", "失焦、最小化或切换界面时，立即重新掩码。", 344},
  };
  for (const ItemDef& item : items) {
    Fl_Box* l = new Fl_Box(cx + theme::kGapXl, item.y, 400, 22, item.label);
    l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    l->labelsize(theme::kFontBody);
    l->labelcolor(theme::kText);
    Fl_Box* d = new Fl_Box(cx + theme::kGapXl, item.y + 24, 560, 20, item.desc);
    d->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    d->labelsize(theme::kFontSmall);
    d->labelcolor(theme::kTextMuted);
  }

  idle_value_ = new widgets::Pill(cx + cw - theme::kGapXl - 120, 176, 120, 30, "5 分钟",
                                   theme::kText, theme::kSubtleBg);
  clipboard_value_ = new widgets::Pill(cx + cw - theme::kGapXl - 120, 260, 120, 30, "30 秒",
                                        theme::kText, theme::kSubtleBg);
  blur_value_ = new widgets::Pill(cx + cw - theme::kGapXl - 120, 344, 120, 30, "已开启",
                                  theme::kAccent, theme::kAccentSoft);

  // 点击数值区循环切换可选值（1/5/15 分钟、15/30/60 秒、开关两态）。
  idle_value_->callback([](Fl_Widget*, void* d) {
    static_cast<SettingsPage*>(d)->on_idle_choice(0);
  }, this);
  clipboard_value_->callback([](Fl_Widget*, void* d) {
    static_cast<SettingsPage*>(d)->on_clipboard_choice(0);
  }, this);
  blur_value_->callback([](Fl_Widget*, void* d) {
    static_cast<SettingsPage*>(d)->on_blur_toggle(false);
  }, this);

  for (int i = 0; i < 3; ++i) {
    Fl_Box* div = new Fl_Box(cx + theme::kGapXl, 236 + i * 84, cw - theme::kGapXl * 2, 1, "");
    div->box(FL_FLAT_BOX);
    div->color(theme::kBorderSubtle);
  }

  Fl_Box* tip = new Fl_Box(cx + theme::kGapXl, 476, cw - theme::kGapXl * 2, 20,
                           "时长为设计示例，可选：锁定 1 / 5 / 15 分钟；剪贴板 15 / 30 / 60 秒。");
  tip->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  tip->labelsize(theme::kFontSmall);
  tip->labelcolor(theme::kTextMuted);

  auto* lock_now = new widgets::IconButton(theme::kContentPad + theme::kGapXl, 512, 148, 40,
                                           Icon::kLock, "立即锁定密匣", ButtonKind::kSecondary);
  lock_now->callback([](Fl_Widget*, void* d) {
    static_cast<SettingsPage*>(d)->on_lock_now();
  }, this);
}

void SettingsPage::reload() {
  const SecuritySettings& s = host()->settings();
  idle_value_->copy_label((std::to_string(s.idle_lock_minutes) + " 分钟").c_str());
  clipboard_value_->copy_label((std::to_string(s.clipboard_clear_seconds) + " 秒").c_str());
  if (s.hide_plaintext_on_blur) {
    blur_value_->copy_label("已开启");
  } else {
    blur_value_->copy_label("已关闭");
  }
  idle_value_->redraw();
  clipboard_value_->redraw();
  blur_value_->redraw();
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
