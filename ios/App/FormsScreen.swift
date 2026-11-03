import SwiftUI
import SecretCore

/// 覆盖表单路由（画板 12/13/17/18）。
enum SkForm {
    case createKey
    case addSecret
    case importKey
    case importSecret
}

/// 表单页公共骨架：顶部返回条 + 滚动容器。
struct FormScaffold: View {
    @EnvironmentObject var state: AppViewModel
    let form: SkForm
    let onBack: () -> Void

    var body: some View {
        Group {
            switch form {
            case .createKey: CreateKeyForm(onBack: onBack)
            case .addSecret: AddSecretForm(onBack: onBack)
            case .importKey: ImportKeyForm(onBack: onBack)
            case .importSecret: ImportSecretForm(onBack: onBack)
            }
        }
        .navigationBarBackButtonHidden(true)
        .navigationTitle(form.title)
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .navigationBarLeading) {
                Button { onBack() } label: {
                    Image(systemName: "chevron.left")
                }
            }
        }
    }
}

extension SkForm {
    var title: String {
        switch self {
        case .createKey: return "生成主密钥"
        case .addSecret: return "新增机密信息"
        case .importKey: return "导入主密钥"
        case .importSecret: return "导入机密信息"
        }
    }
}

// ------------------------------------------------------------- 生成主密钥

private struct CreateKeyForm: View {
    @EnvironmentObject var state: AppViewModel
    let onBack: () -> Void
    @State private var name = ""
    @State private var pw = ""
    @State private var confirm = ""

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text("在本机生成一把随机 RSA-2048 主密钥。")
                    .font(.system(size: 13)).foregroundColor(SkColors.textSub)
                Text("主密钥名称（可选）").font(.system(size: 13))
                TextField("可以留空", text: $name)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never)
                Text("名称仅用于辨识，不参与加密。")
                    .font(.system(size: 11)).foregroundColor(SkColors.textSub)

                Text("主密钥密码").font(.system(size: 13))
                SecureField("输入主密钥密码", text: $pw)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never)
                Text("确认主密钥密码").font(.system(size: 13))
                SecureField("再次输入主密钥密码", text: $confirm)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never)
                if !confirm.isEmpty && pw != confirm {
                    Text("两次输入的密码不一致")
                        .font(.system(size: 12)).foregroundColor(SkColors.dangerText)
                }

                VStack(alignment: .leading, spacing: 4) {
                    Text("请妥善记住主密钥密码")
                        .font(.system(size: 13, weight: .semibold))
                    Text("密码丢失无法解密，密匣无法为你重置密码。")
                        .font(.system(size: 12))
                }
                .foregroundColor(SkColors.warnText)
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(SkColors.warnBg)
                .cornerRadius(8)

                if let msg = state.formError {
                    Text(msg).font(.system(size: 13)).foregroundColor(SkColors.dangerText)
                }

                Button("生成主密钥") {
                    if state.createMasterKey(name.trimmingCharacters(in: .whitespaces), pw) {
                        onBack()
                    }
                }
                .disabled(pw.isEmpty || pw != confirm)
                .buttonStyle(.borderedProminent)
                .padding(.top, 6)
            }
            .padding(20)
        }
        .background(SkColors.pageBg)
    }
}

// ------------------------------------------------------------ 新增机密信息

private struct AddSecretForm: View {
    @EnvironmentObject var state: AppViewModel
    let onBack: () -> Void
    @State private var title = ""
    @State private var content = ""
    @State private var selectedId = ""
    @State private var mkPassword = ""

    private var overLimit: Bool {
        content.unicodeScalars.count > ServiceLimits.maxSecretChars
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                HStack {
                    Text("自由文本，本地加密保存。")
                        .font(.system(size: 13)).foregroundColor(SkColors.textSub)
                    Spacer()
                    Text("\(content.unicodeScalars.count) / \(ServiceLimits.maxSecretChars)")
                        .font(.system(size: 13)).foregroundColor(SkColors.textSub)
                }

                Text("用于加密的主密钥").font(.system(size: 13))
                ForEach(state.masterKeys, id: \.masterKeyId) { item in
                    KeyPickRow(item: item,
                               selected: item.masterKeyId == selectedId) {
                        selectedId = item.masterKeyId
                    }
                }
                Text("空名称保持留白，可通过 ID 辨识；选该密钥需输密码。")
                    .font(.system(size: 11)).foregroundColor(SkColors.textFaint)

                Text("标题（可选）").font(.system(size: 13))
                TextField("不填写则保持为空", text: $title)
                    .textFieldStyle(.roundedBorder)

                Text("机密信息").font(.system(size: 13))
                TextEditor(text: $content)
                    .frame(height: 120)
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .stroke(overLimit ? SkColors.dangerText : SkColors.border)
                    )
                HStack {
                    Text("最多 150 字符").font(.system(size: 11))
                        .foregroundColor(SkColors.textFaint)
                    Spacer()
                    if overLimit {
                        Text("超出 150 字符").font(.system(size: 11))
                            .foregroundColor(SkColors.dangerText)
                    }
                }

                // 非默认密钥需要该密钥的密码（kNeedsPassword 的表单化）
                if selectedId != state.defaultKeyId {
                    Text("「\(keyName(selectedId))」的密码").font(.system(size: 13))
                    SecureField("输入该主密钥的密码", text: $mkPassword)
                        .textFieldStyle(.roundedBorder)
                        .textInputAutocapitalization(.never)
                }

                if let msg = state.formError {
                    Text(msg).font(.system(size: 13)).foregroundColor(SkColors.dangerText)
                }

                Button {
                    if state.addSecret(title.trimmingCharacters(in: .whitespaces),
                                       content, selectedId, mkPassword) {
                        onBack()
                    }
                } label: {
                    Label("保存", systemImage: "lock.fill")
                        .frame(maxWidth: .infinity)
                        .frame(height: 44)
                }
                .disabled(content.isEmpty || overLimit)
                .buttonStyle(.borderedProminent)

                Text("AES-256-GCM 加密内容；RSA-OAEP-SHA256 保护数据密钥。")
                    .font(.system(size: 11)).foregroundColor(SkColors.textFaint)
            }
            .padding(20)
        }
        .background(SkColors.pageBg)
        .onAppear {
            if selectedId.isEmpty {
                selectedId = state.defaultKeyId.isEmpty
                    ? state.masterKeys.first?.masterKeyId ?? ""
                    : state.defaultKeyId
            }
        }
    }

    private func keyName(_ id: String) -> String {
        state.masterKeys.first { $0.masterKeyId == id }?.name ?? shortId(id)
    }
}

private struct KeyPickRow: View {
    let item: MasterKeyListItem
    let selected: Bool
    let onPick: () -> Void

    var body: some View {
        Button(action: onPick) {
            HStack(spacing: 10) {
                Image(systemName: selected ? "checkmark.circle.fill" : "circle")
                    .foregroundColor(selected ? SkColors.primary : SkColors.textFaint)
                // 空名称保持留白（需求 3.1）
                Text(item.name)
                    .font(.system(size: 14, weight: .medium))
                    .foregroundColor(SkColors.textMain)
                Spacer()
                Text(shortId(item.masterKeyId))
                    .font(.system(size: 13)).foregroundColor(SkColors.textSub)
            }
            .padding(12)
            .background(selected ? SkColors.primaryWash : SkColors.cardBg)
            .cornerRadius(8)
        }
        .buttonStyle(.plain)
    }
}

// ------------------------------------------------------------- 导入表单

/// 文件选择卡（画板 13/18 共用）。
private struct FilePickCard: View {
    @Binding var fileName: String
    @Binding var bytes: [UInt8]?
    let title: String

    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: title.contains("主密钥") ? "key.fill" : "doc.fill")
                .font(.system(size: 30))
                .foregroundColor(SkColors.primary)
            Text(fileName.isEmpty ? "未选择文件" : fileName)
                .font(.system(size: 15, weight: .semibold))
                .foregroundColor(fileName.isEmpty ? SkColors.textFaint : SkColors.textMain)
            Text("本地文件")
                .font(.system(size: 12)).foregroundColor(SkColors.textSub)
        }
        .frame(maxWidth: .infinity)
        .padding(16)
        .background(SkColors.cardBg)
        .cornerRadius(12)
    }
}

private struct ImportKeyForm: View {
    @EnvironmentObject var state: AppViewModel
    let onBack: () -> Void
    @State private var fileName = ""
    @State private var bytes: [UInt8]?
    @State private var protPw = ""
    @State private var newPw = ""

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text("使用导出文件的保护密码，导入为主密钥。")
                    .font(.system(size: 13)).foregroundColor(SkColors.textSub)
                FilePickCard(fileName: $fileName, bytes: $bytes, title: "导入主密钥")
                    .textSelection(.enabled)
                    .overlay(
                        // 轻量文件读取：从分享/文件 App 拖入在 v0.0.1 暂不支持，
                        // 用文档目录路径输入兜底（TODO I2：fileImporter）。
                        Group { EmptyView() }
                    )
                Text("已读入字节数：\(bytes?.count ?? 0)（从应用文档目录读取同 .smk 文件名）")
                    .font(.system(size: 11)).foregroundColor(SkColors.textFaint)

                Text("文件保护密码").font(.system(size: 13))
                SecureField("输入导出时设置的保护密码", text: $protPw)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never)

                Text("新的主密钥密码").font(.system(size: 13))
                SecureField("为本机保存的主密钥新设密码", text: $newPw)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never)

                if state.masterKeys.count >= 2 {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("⚠ 容量已满 · \(state.masterKeys.count)/2")
                            .font(.system(size: 13, weight: .semibold))
                        Text("先导出并删除一把主密钥，再导入此文件。")
                            .font(.system(size: 12))
                    }
                    .foregroundColor(SkColors.warnText)
                    .padding(12)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(SkColors.warnBg)
                    .cornerRadius(8)
                }

                if let msg = state.formError {
                    Text(msg).font(.system(size: 13)).foregroundColor(SkColors.dangerText)
                }

                Button("导入主密钥") {
                    if let b = bytes,
                       state.importMasterKey(b, protPw, newPw) { onBack() }
                }
                .disabled(bytes == nil || protPw.isEmpty || newPw.isEmpty)
                .buttonStyle(.borderedProminent)
                .padding(.top, 6)

                Text("主密钥密码丢失无法解密。名称来自文件，可保留为空；第一把主密钥自动设为默认。")
                    .font(.system(size: 11)).foregroundColor(SkColors.textFaint)
            }
            .padding(20)
        }
        .background(SkColors.pageBg)
        .onAppear { readFileFromDocs() }
    }

    /// v0.0.1 文件读取兜底：从 App 文档目录找 .smk 文件（AirDrop/文件 App 放入）。
    private func readFileFromDocs() {
        let docs = FileManager.default.urls(for: .documentDirectory,
                                            in: .userDomainMask)[0]
        if let url = (try? FileManager.default.contentsOfDirectory(
            at: docs, includingPropertiesForKeys: nil))?
            .first(where: { $0.pathExtension == "smk" }) {
            fileName = url.lastPathComponent
            bytes = (try? Data(contentsOf: url)).map { [UInt8]($0) } ?? nil
        }
    }
}

private struct ImportSecretForm: View {
    @EnvironmentObject var state: AppViewModel
    let onBack: () -> Void
    @State private var fileName = ""
    @State private var bytes: [UInt8]?
    @State private var mkPw = ""

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text("导入导出的 .ssc 文件；数据密钥由主密钥封装。")
                    .font(.system(size: 13)).foregroundColor(SkColors.textSub)
                FilePickCard(fileName: $fileName, bytes: $bytes, title: "导入机密信息")
                Text("已读入字节数：\(bytes?.count ?? 0)（从应用文档目录读取同 .ssc 文件名）")
                    .font(.system(size: 11)).foregroundColor(SkColors.textFaint)

                Text("主密钥密码").font(.system(size: 13))
                SecureField("输入对应主密钥的密码", text: $mkPw)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never)
                Text("导入前会先验证能解开数据；ID 冲突时自动换新 ID，不覆盖本地记录。")
                    .font(.system(size: 12)).foregroundColor(SkColors.textSub)

                if let msg = state.formError {
                    Text(msg).font(.system(size: 13)).foregroundColor(SkColors.dangerText)
                }

                Button("导入机密信息") {
                    if let b = bytes, state.importSecret(b, mkPw) { onBack() }
                }
                .disabled(bytes == nil || mkPw.isEmpty)
                .buttonStyle(.borderedProminent)
                .padding(.top, 6)
            }
            .padding(20)
        }
        .background(SkColors.pageBg)
        .onAppear { readFileFromDocs() }
    }

    private func readFileFromDocs() {
        let docs = FileManager.default.urls(for: .documentDirectory,
                                            in: .userDomainMask)[0]
        if let url = (try? FileManager.default.contentsOfDirectory(
            at: docs, includingPropertiesForKeys: nil))?
            .first(where: { $0.pathExtension == "ssc" }) {
            fileName = url.lastPathComponent
            bytes = (try? Data(contentsOf: url)).map { [UInt8]($0) } ?? nil
        }
    }
}
