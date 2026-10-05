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

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  Fl::visual(FL_RGB);
  secretkeeper::ui::MainWindow window;
  window.show();
  return Fl::run();
}
