import SwiftUI
import SecretCore

/// 画板 11：主密钥管理。生成/导入入口、密钥卡（默认徽章、导出/删除/设为默认）、
/// 配额提示条（业务规则：2 把，达上限时黄色提示条）。
struct KeysScreen: View {
    @EnvironmentObject var state: AppViewModel
    let onOpenCreate: () -> Void
    let onOpenImport: () -> Void

    @State private var switchTargetId: String?
    @State private var deleteTargetId: String?
    @State private var exportProtectionPw = ""
    @State private var pendingExportKey: String?
    @State private var showExportDialog = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                Text("主密钥管理")
                    .font(.system(size: 22, weight: .semibold))
                    .padding(.bottom, 16)

                HStack(spacing: 12) {
                    Button("生成主密钥", action: onOpenCreate)
                        .buttonStyle(.bordered)
                        .frame(maxWidth: .infinity)
                    Button("导入主密钥", action: onOpenImport)
                        .buttonStyle(.bordered)
                        .frame(maxWidth: .infinity)
                }
                .padding(.bottom, 18)

                HStack {
                    Text("本机保存的主密钥").font(.system(size: 13))
                        .foregroundColor(SkColors.textSub)
                    Spacer()
                    Text("\(state.masterKeys.count) / 2 把")
                        .font(.system(size: 13))
                        .foregroundColor(SkColors.warnText)
                }
                .padding(.bottom, 10)

                ForEach(state.masterKeys, id: \.masterKeyId) { item in
                    MasterKeyCard(item: item,
                        onExport: {
                            pendingExportKey = item.masterKeyId
                            showExportDialog = true
                        },
                        onSwitchDefault: { switchTargetId = item.masterKeyId },
                        onDelete: { deleteTargetId = item.masterKeyId }
                    )
                    .padding(.bottom, 12)
                }

                if state.masterKeys.count >= 2 {
                    Text("⚠ 主密钥数量已满，先导出并删除一把主密钥，才能新增。")
                        .font(.system(size: 13))
                        .foregroundColor(SkColors.warnText)
                        .padding(12)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(SkColors.warnBg)
                        .cornerRadius(8)
                }
            }
            .padding(20)
        }
        .background(SkColors.pageBg)
        .sheet(isPresented: $showExportDialog) {
            ExportProtectionDialog() { name in
                exportTarget(name: name)
            }
        }
        .sheet(isPresented: Binding(get: { switchTargetId != nil },
                                    set: { if !$0 { switchTargetId = nil } })) {
            if let target = switchTargetId {
                SwitchDefaultDialog(targetId: target) {
                    switchTargetId = nil
                }
            }
        }
        .sheet(isPresented: Binding(get: { deleteTargetId != nil },
                                    set: { if !$0 { deleteTargetId = nil } })) {
            if let target = deleteTargetId {
                DeleteKeyDialog(targetId: target) {
                    deleteTargetId = nil
                }
            }
        }
    }

    /// 导出主密钥：保护密码校验通过后落盘到 App 文档目录（简化 v0.0.1）。
    private func exportTarget(name: String) {
        guard let key = pendingExportKey else { return }
        let (e, bytes) = state.exportMasterKey(key, exportProtectionPw)
        if e == .ok, let bytes {
            let docs = FileManager.default.urls(for: .documentDirectory,
                                                in: .userDomainMask)[0]
            let url = docs.appendingPathComponent("masterkey-\(key.suffix(8)).smk")
            try? Data(bytes).write(to: url)
        }
        pendingExportKey = nil
        exportProtectionPw = ""
        _ = name
    }
}

private struct MasterKeyCard: View {
    let item: MasterKeyListItem
    let onExport: () -> Void
    let onSwitchDefault: () -> Void
    let onDelete: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 8) {
                Image(systemName: "key.fill").foregroundColor(SkColors.primary)
                Text(item.name.isEmpty ? shortId(item.masterKeyId) : item.name)
                    .font(.system(size: 16, weight: .semibold))
                Spacer()
                if item.isDefault {
                    Text("默认").font(.system(size: 12))
                        .foregroundColor(SkColors.primary)
                        .padding(.horizontal, 8).padding(.vertical, 3)
                        .background(SkColors.primaryWash)
                        .cornerRadius(6)
                }
            }
            .padding(.bottom, 12)

            HStack {
                Text("主密钥 ID").font(.system(size: 13)).foregroundColor(SkColors.textSub)
                    .frame(width: 80, alignment: .leading)
                Text(shortId(item.masterKeyId)).font(.system(size: 13))
            }
            .padding(.bottom, 6)
            Text("RSA-2048 · 关联 \(item.secretCount) 条机密信息")
                .font(.system(size: 13))
                .foregroundColor(SkColors.textSub)
                .padding(.bottom, 12)

            HStack(spacing: 10) {
                Button("导出主密钥", action: onExport).buttonStyle(.bordered)
                Button("设为默认", action: onSwitchDefault)
                    .buttonStyle(.borderedProminent)
                    .disabled(item.isDefault)
                Button("删除", action: onDelete)
                    .foregroundColor(SkColors.dangerText)
                    .disabled(item.isDefault)
            }
            if item.isDefault {
                Text("默认主密钥禁止删除。请先切换默认。")
                    .font(.system(size: 12))
                    .foregroundColor(SkColors.textSub)
                    .padding(.top, 8)
            }
        }
        .padding(16)
        .background(SkColors.cardBg)
        .cornerRadius(12)
    }
}

/// 导出保护密码对话框（保护密码与主密钥密码相互独立）。
private struct ExportProtectionDialog: View {
    @EnvironmentObject var state: AppViewModel
    @Environment(\.dismiss) private var dismiss
    let onConfirm: (String) -> Void
    @State private var pw = ""

    var body: some View {
        NavigationStack {
            VStack(alignment: .leading, spacing: 10) {
                Text("保护密码用于加密导出的文件，与主密钥密码相互独立。")
                    .font(.system(size: 13))
                    .foregroundColor(SkColors.textSub)
                SecureField("输入保护密码（不可为空）", text: $pw)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never)
                Spacer()
            }
            .padding(16)
            .navigationTitle("设置导出保护密码")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("取消") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("选择保存位置") { onConfirm(pw); dismiss() }
                        .disabled(pw.isEmpty)
                }
            }
        }
    }
}

/// 切换默认主密钥（需求 2.5：两把密钥的密码都需验证；已解锁时可留空）。
private struct SwitchDefaultDialog: View {
    @EnvironmentObject var state: AppViewModel
    @Environment(\.dismiss) private var dismiss
    let targetId: String
    let onDone: () -> Void
    @State private var curPw = ""
    @State private var newPw = ""

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Text("已解锁：两把密钥的密码均可留空，用已解锁的密钥缓存校验。")
                        .font(.system(size: 12))
                        .foregroundColor(SkColors.textSub)
                    SecureField("原默认主密钥密码（可留空）", text: $curPw)
                    SecureField("新默认主密钥密码（可留空）", text: $newPw)
                    if let msg = state.formError {
                        Text(msg).font(.system(size: 12)).foregroundColor(SkColors.dangerText)
                    }
                }
                Section {
                    Button("切换") {
                        if state.switchDefault(targetId, curPw, newPw) { onDone() }
                    }
                }
            }
            .navigationTitle("切换默认主密钥")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("取消") { dismiss() }
                }
            }
        }
    }
}

/// 删除主密钥确认（关联机密信息保留、显示黄色问号）。
private struct DeleteKeyDialog: View {
    @EnvironmentObject var state: AppViewModel
    @Environment(\.dismiss) private var dismiss
    let targetId: String
    let onDone: () -> Void
    @State private var pw = ""

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Text("删除「\(state.masterKeys.first { $0.masterKeyId == targetId }?.name ?? shortId(targetId))」。"
                         + "其加密的机密信息会保留（显示黄色问号），删除前建议先导出备份。")
                        .font(.system(size: 13))
                        .foregroundColor(SkColors.textSub)
                    SecureField("输入主密钥密码", text: $pw)
                    if let msg = state.formError {
                        Text(msg).font(.system(size: 12)).foregroundColor(SkColors.dangerText)
                    }
                }
                Section {
                    Button("删除", role: .destructive) {
                        if state.deleteMasterKey(targetId, pw) { onDone() }
                    }
                    .disabled(pw.isEmpty)
                }
            }
            .navigationTitle("删除主密钥")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("取消") { dismiss() }
                }
            }
        }
    }
}
