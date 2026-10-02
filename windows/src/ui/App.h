#pragma once

#include <winrt/Microsoft.UI.Xaml.h>

namespace winrt::SecretKeeper::implementation {

struct App : winrt::Microsoft::UI::Xaml::Application {
  App();

  void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const& e);
};

}  // namespace winrt::SecretKeeper::implementation
