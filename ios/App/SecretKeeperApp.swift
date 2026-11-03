import SwiftUI
import SecretCore

/// 密匣 iOS 端入口。路由：解锁页 ↔ 主界面（三 tab）↔ 表单/详情覆盖层。
/// 与 Android MainActivity.kt 同构；数据目录为 Application Support（docs/03-data §6.1）。
@main
struct SecretKeeperApp: App {
    @StateObject private var state: AppViewModel

    init() {
        let fm = FileManager.default
        let base = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? fm.temporaryDirectory
        let dir = base.appendingPathComponent("SecretKeeper", isDirectory: true)
        _state = StateObject(wrappedValue: AppViewModel(dataDir: dir.path))
    }

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(state)
        }
    }
}

struct RootView: View {
    @EnvironmentObject var state: AppViewModel
    @State private var tab: SkTab = .secrets
    @State private var form: SkForm?
    @State private var detailId: String?

    var body: some View {
        if state.unlocked {
            if let id = detailId {
                DetailScreen(secretId: id, onBack: { detailId = nil })
            } else if let f = form {
                FormScaffold(form: f, onBack: { form = nil })
            } else {
                MainTabs(tab: $tab, onOpenForm: { form = $0 },
                         onOpenDetail: { id in
                             state.loadDetail(id)
                             detailId = id
                         })
            }
        } else {
            UnlockScreen()
        }
    }
}
