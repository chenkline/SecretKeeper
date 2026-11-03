import SwiftUI
import SecretCore

/// 画板 14：机密信息列表。搜索 + 行卡 + 配额；黄色问号 = 密钥缺失；
/// 空标题/空名保持留白（需求 3.1）。
struct SecretsScreen: View {
    @EnvironmentObject var state: AppViewModel
    let onOpenAdd: () -> Void
    let onOpenImport: () -> Void
    let onOpenDetail: (String) -> Void
    @State private var query = ""

    private var filtered: [SecretListItem] {
        state.secrets.filter { s in
            query.isEmpty
                || s.title.lowercased().contains(query.lowercased())
                || s.masterKeyId.lowercased().contains(query.lowercased())
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("机密信息").font(.system(size: 22, weight: .semibold))
                Spacer()
                Image(systemName: "lock.fill").foregroundColor(SkColors.primary)
            }
            .padding(.bottom, 14)

            HStack {
                Text(state.quotaText).font(.system(size: 13)).foregroundColor(SkColors.textSub)
                Spacer()
                Button("导入", action: onOpenImport).buttonStyle(.bordered)
                Button {
                    onOpenAdd()
                } label: {
                    Label("新增", systemImage: "plus")
                }
                .buttonStyle(.borderedProminent)
            }
            .padding(.bottom, 12)

            TextField("搜索标题或主密钥 ID", text: $query)
                .textFieldStyle(.roundedBorder)
                .textInputAutocapitalization(.never)
                .padding(.bottom, 10)

            Text("标题（可选）／主密钥 ID／主密钥名称")
                .font(.system(size: 11))
                .foregroundColor(SkColors.textFaint)
                .padding(.bottom, 4)

            if filtered.isEmpty {
                Spacer()
                Text(state.secrets.isEmpty ? "暂无机密信息，点「新增」创建第一条。"
                                           : "无匹配结果")
                    .font(.system(size: 13))
                    .foregroundColor(SkColors.textFaint)
                    .frame(maxWidth: .infinity)
                Spacer()
            } else {
                List(filtered, id: \.secretId) { item in
                    Button { onOpenDetail(item.secretId) } label: {
                        SecretRow(item)
                    }
                    .listRowBackground(SkColors.cardBg)
                    .listRowSeparator(.hidden)
                }
                .listStyle(.plain)
            }

            Text("黄色？表示主密钥缺失；空名称不代表缺失。空标题与空名称保持留白。")
                .font(.system(size: 11))
                .foregroundColor(SkColors.textFaint)
                .padding(.vertical, 8)
        }
        .padding(20)
        .background(SkColors.pageBg)
    }
}

private struct SecretRow: View {
    let item: SecretListItem

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                // 空标题与空名称保持留白（需求 3.1），不显示占位符
                Text(item.title)
                    .font(.system(size: 15, weight: .medium))
                    .foregroundColor(SkColors.textMain)
                HStack(spacing: 12) {
                    Text(shortId(item.masterKeyId) + (item.masterKeyFound ? "" : "?"))
                        .font(.system(size: 13))
                        .foregroundColor(item.masterKeyFound
                            ? SkColors.textSub : SkColors.warnText)
                    if !item.masterKeyName.isEmpty {
                        Text(item.masterKeyName)
                            .font(.system(size: 13))
                            .foregroundColor(SkColors.textSub)
                    }
                }
            }
            Spacer()
            Text("›").font(.system(size: 18)).foregroundColor(SkColors.textFaint)
        }
        .padding(.vertical, 10)
        .contentShape(Rectangle())
    }
}
