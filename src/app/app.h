#pragma once

// SecretKeeper - 应用入口声明。
//
// 界面实现在 main_window.cpp 与 pages.cpp。这里只暴露启动函数，
// 供三端各自的入口（若将来需要不同的 main）复用。

namespace secretkeeper::ui {

// 启动界面并进入事件循环，返回进程退出码。
int run(int argc, char** argv);

}  // namespace secretkeeper::ui
