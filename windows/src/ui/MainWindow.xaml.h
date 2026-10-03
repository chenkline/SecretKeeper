#pragma once

#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"

namespace winrt::SecretKeeper::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
};

}  // namespace winrt::SecretKeeper::implementation
