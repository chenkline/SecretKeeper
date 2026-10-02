#include "App.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Windows::ApplicationModel::Activation;
using namespace Microsoft::UI::Xaml;
using namespace SecretKeeper::implementation;

App::App() {}

void App::OnLaunched(LaunchActivatedEventArgs const&) {
  winrt::SecretKeeper::MainWindow().Activate();
}
