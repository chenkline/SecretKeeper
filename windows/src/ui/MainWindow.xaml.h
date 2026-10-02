#pragma once

#include "MainWindow.g.h"

namespace winrt::SecretKeeper::implementation {

struct MainWindow : MainWindowT<MainWindow> {
  MainWindow();
};

}  // namespace winrt::SecretKeeper::implementation

namespace winrt::SecretKeeper::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::SecretKeeper::factory_implementation
