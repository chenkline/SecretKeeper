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
    @Environment(\.scenePhase) private var scenePhase
    @State private var tab: SkTab = .secrets
    @State private var form: SkForm?
    @State private var detailId: String?
    @State private var lastActivity = Date()

    // 需求 4.1：闲置 5 分钟自动锁定（与 v1.5 桌面默认一致）。
    private let idleLimit: TimeInterval = 5 * 60

    var body: some View {
        if state.unlocked {
            // 需求 4.1：回到前台时若已闲置超限则锁定（后台到达即视为超限起点）。
            if Date().timeIntervalSince(lastActivity) >= idleLimit {
                state.lock()
            }
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
        .onChange(of: scenePhase) { phase in
            // 前台交互重置闲置计时；进入后台 5 分钟无交互即锁定。
            if phase == .active {
                lastActivity = Date()
            } else if phase == .background {
                lastActivity = Date().addingTimeInterval(-idleLimit)
            }
        }
    }
}
