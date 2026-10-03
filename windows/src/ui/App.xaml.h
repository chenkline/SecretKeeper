#pragma once

// #include "App.g.h" 去掉，改为下面的App.xaml.g.h  
#include "App.xaml.g.h" // 新增：必须包含 XAML 生成的 AppT<App> 与 InitializeComponent

namespace winrt::SecretKeeper::implementation {

struct App : AppT<App> {          // 改为 AppT<App>，不是 ApplicationT<App>
    App();                        // 声明，不是 = default
    void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
};

}  // namespace winrt::SecretKeeper::implementation