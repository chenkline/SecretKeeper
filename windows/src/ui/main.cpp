// SecretKeeper - 入口点。
//
// WinUI 3 要求用 wWinMain 而非 main，且必须先初始化 COM apartment。
// WinUI 3 桌面应用不能使用 CoreApplication::Run，人工手动改为 Application::Start

#include <unknwn.h> // 必须放在所有 C++/WinRT 头文件之前
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include "App.xaml.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    ::winrt::Microsoft::UI::Xaml::Application::Start(
        [](auto&&) {
            ::winrt::make<::winrt::SecretKeeper::implementation::App>();
        });
    return 0;
}
