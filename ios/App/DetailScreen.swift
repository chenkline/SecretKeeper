import SwiftUI
import SecretCore

/// 画板 15/16：机密信息详情。掩码固定 6 星；查看后显示明文；切换界面或
/// 进入后台立即隐藏（scenePhase 监听，需求 3.5）；删除走确认。
struct DetailScreen: View {
    @EnvironmentObject var state: AppViewModel
    @Environment(\.scenePhase) private var scenePhase
    let secretId: String
    let onBack: () -> Void

    @State private var showReveal = false
    @State private var showDelete = false
    @State private var revealPw = ""

    var body: some View {
        Group {
            if let d = state.detail {
                content(d)
            } else {
                Text(state.detailError ?? "条目不存在")
                    .foregroundColor(SkColors.dangerText)
                    .padding(20)
            }
        }
        .background(SkColors.pageBg)
        .navigationBarBackButtonHidden(true)
        .navigationTitle("信息详情")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .navigationBarLeading) {
                Button { onBack() } label: {
                    Image(systemName: "chevron.left")
                }
            }
        }
        .onChange(of: scenePhase) { phase in
            // 需求 3.5：切换界面或进入后台，立即隐藏明文。
            if phase != .active { state.maskDetail() }
        }
        .sheet(isPresented: $showReveal) {
            RevealSheet(secretId: secretId) { showReveal = false }
        }
        .sheet(isPresented: $showDelete) {
            DeleteSecretSheet(secretId: secretId) {
                showDelete = false
                onBack()
            }
        }
        .onAppear {
            if state.detail?.secretId != secretId { state.loadDetail(secretId) }
        }
    }

    @ViewBuilder
    private func content(_ d: SecretDetail) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                SkCard {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack {
                            Text("关联主密钥")
                                .font(.system(size: 16, weight: .semibold))
                            Spacer()
                            Image(systemName: d.masterKeyFound
                                ? "checkmark.circle" : "questionmark.circle")
                                .foregroundColor(d.masterKeyFound
                                    ? SkColors.primary : SkColors.warnText)
                        }
                        DetailLine(label: "主密钥 ID",
                                   value: shortId(d.masterKeyId)
                                       + (d.masterKeyFound ? "" : "?"))
                        DetailLine(label: "主密钥名称", value: d.masterKeyName) // 空名保持留白
                    }
                }

                SkCard {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Text("标题").font(.system(size: 13))
                                .foregroundColor(SkColors.textSub)
                                .frame(width: 56, alignment: .leading)
                            Text(d.title).font(.system(size: 14))
                        }
                        HStack {
                            Text("机密信息内容")
                                .font(.system(size: 14, weight: .semibold))
                            Spacer()
                            if !d.plaintextMasked {
                                Button { state.maskDetail() } label: {
                                    Label("隐藏", systemImage: "eye.slash")
                                        .font(.system(size: 12))
                                        .foregroundColor(SkColors.textSub)
                                }
                            }
                        }
                        Text(d.plaintextMasked ? ServiceMaskedText : d.plaintext)
                            .font(.system(size: 14))
                            .foregroundColor(d.plaintextMasked
                                ? SkColors.textSub : SkColors.textMain)
                            .frame(maxWidth: .infinity, minHeight: 90,
                                   alignment: .topLeading)
                            .padding(14)
                            .background(SkColors.pageBg)
                            .cornerRadius(8)
                    }
                }

                Text("默认掩码，查看后才显示。切换界面或进入后台，立即隐藏明文。")
                    .font(.system(size: 12))
                    .foregroundColor(SkColors.primary)
                    .padding(12)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(SkColors.primaryWash)
                    .cornerRadius(8)

                if let msg = state.detailError {
                    Text(msg).font(.system(size: 13)).foregroundColor(SkColors.dangerText)
                }

                Button("删除机密信息") { showDelete = true }
                    .foregroundColor(SkColors.dangerText)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 12)
                    .background(SkColors.dangerBg)
                    .cornerRadius(8)
            }
            .padding(20)
        }
        .safeAreaInset(edge: .bottom) {
            if !d.plaintextMasked {
                HStack(spacing: 10) {
                    Button {
                        UIPasteboard.general.string = d.plaintext
                        // 需求 4.3：30 秒后清除剪贴板。
                        Timer.scheduledTimer(withTimeInterval: 30, repeats: false) { _ in
                            UIPasteboard.general.string = nil
                        }
                    } label: {
                        Label("复制", systemImage: "doc.on.doc")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    Button {
                        state.requestSecretExport = secretId
                    } label: {
                        Label("导出", systemImage: "square.and.arrow.up")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                }
                .padding(12)
            }
        }
    }
}

/// 需求 3.5 的固定掩码（6 个星号），UI 不得自造。
let ServiceMaskedText = "******"

private struct DetailLine: View {
    let label: String
    let value: String

    var body: some View {
        HStack {
            Text(label).font(.system(size: 13))
                .foregroundColor(SkColors.textSub)
                .frame(width: 84, alignment: .leading)
            Text(value).font(.system(size: 13))
        }
    }
}

/// 查看密码弹层。
private struct RevealSheet: View {
    @EnvironmentObject var state: AppViewModel
    @Environment(\.dismiss) private var dismiss
    let secretId: String
    let onDone: () -> Void
    @State private var pw = ""

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Text("「主密钥」")
                        .font(.system(size: 13)).foregroundColor(SkColors.textSub)
                    SecureField("输入主密钥密码", text: $pw)
                    if let msg = state.detailError {
                        Text(msg).font(.system(size: 12))
                            .foregroundColor(SkColors.dangerText)
                    }
                }
                Section {
                    Button("查看") {
                        state.revealDetail(secretId, pw)
                        onDone()
                    }
                    .disabled(pw.isEmpty)
                }
            }
            .navigationTitle("验证主密钥密码")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("取消") { dismiss() }
                }
            }
        }
    }
}

/// 删除机密信息确认（画板 19：固定建议先导出备份）。
private struct DeleteSecretSheet: View {
    @EnvironmentObject var state: AppViewModel
    @Environment(\.dismiss) private var dismiss
    let secretId: String
    let onDone: () -> Void
    @State private var pw = ""

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Text("删除后无法恢复。建议先导出备份。删除需验证对应主密钥密码。")
                        .font(.system(size: 13))
                        .foregroundColor(SkColors.textSub)
                    SecureField("输入主密钥密码", text: $pw)
                }
                Section {
                    Button("删除", role: .destructive) {
                        if state.deleteSecret(secretId, pw) { onDone() }
                    }
                    .disabled(pw.isEmpty)
                }
            }
            .navigationTitle("删除机密信息")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("取消") { dismiss() }
                }
            }
        }
    }
}
