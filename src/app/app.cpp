// SecretKeeper - 程序入口
//
// 界面实现在 main_window.cpp 与 pages.cpp，本文件只负责创建窗口与运行事件循环。
// 三端共享同一份代码；只有 platform.cpp 逐端不同。
//
// 分层约束（见 AGENTS.md）：本层只依赖 core/service.h（唯一对外门面）、
// view_model.h（可测的展示规则）与 platform.h（必须用 OS API 的四项能力），
// 不得包含 crypto / serialize / store 的任何头文件。

#include "app.h"

#include <FL/Fl.H>

#include "main_window.h"
#include "platform.h"


int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  // 必须在任何窗口与控件构造之前安装，否则构造期间的崩溃捕不到。
  secretkeeper::ui::install_crash_dumper();

  SK_DEBUG_PAUSE("before visual");

  Fl::visual(FL_RGB);
  SK_DEBUG_PAUSE("before MainWindow\n");
  secretkeeper::ui::MainWindow window;
  SK_DEBUG_PAUSE("before show\n");
  // window.show();
  return Fl::run();
}
