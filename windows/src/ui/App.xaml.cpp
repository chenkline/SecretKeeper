#include "App.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::SecretKeeper::implementation {

App::App() {
    InitializeComponent();
}

void App::OnLaunched(LaunchActivatedEventArgs const&) {
    winrt::SecretKeeper::MainWindow().Activate();
}

}  // namespace winrt::SecretKeeper::implementation
