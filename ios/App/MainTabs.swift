import SwiftUI
import UIKit

/// 底部导航三页（画板 11/14/20 的底部栏）。
enum SkTab: String, CaseIterable {
    case secrets = "机密信息"
    case keys = "主密钥"
    case settings = "安全设置"

    var icon: String {
        switch self {
        case .secrets: return "lock.fill"
        case .keys: return "key.fill"
        case .settings: return "gearshape"
        }
    }
}

/// 主界面骨架：内容区 + 底部三 tab 导航。
struct MainTabs: View {

    @EnvironmentObject var state: AppViewModel
    @Binding var tab: SkTab
    let onOpenForm: (SkForm) -> Void
    let onOpenDetail: (String) -> Void

    var body: some View {
        TabView(selection: $tab) {
            SecretsScreen(onOpenAdd: { onOpenForm(.addSecret) },
                          onOpenImport: { onOpenForm(.importSecret) },
                          onOpenDetail: onOpenDetail)
                .tabItem { Label(SkTab.secrets.rawValue, systemImage: SkTab.secrets.icon) }
                .tag(SkTab.secrets)
            KeysScreen(onOpenCreate: { onOpenForm(.createKey) },
                       onOpenImport: { onOpenForm(.importKey) })
                .tabItem { Label(SkTab.keys.rawValue, systemImage: SkTab.keys.icon) }
                .tag(SkTab.keys)
            SettingsScreen()
                .tabItem { Label(SkTab.settings.rawValue, systemImage: SkTab.settings.icon) }
                .tag(SkTab.settings)
        }
        .onAppear {
            state.refreshLists()
            UITabBar.appearance().unselectedItemTintColor = UIColor(
                red: 0x6C/255, green: 0x75/255, blue: 0x7D/255, alpha: 1)
        }
    }
}
