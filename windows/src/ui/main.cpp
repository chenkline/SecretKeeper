// SecretKeeper - 入口点。
//
// WinUI 3 要求用 wWinMain 而非 main，且必须先初始化 COM apartment。

#include <windows.h>
#include <winrt/base.h>
#include "App.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  winrt::init_apartment(winrt::apartment_type::single_threaded);
  ::winrt::Windows::ApplicationModel::Core::CoreApplication::Run(
      [] { winrt::SecretKeeper::implementation::App().Start(); });
  return 0;
}
