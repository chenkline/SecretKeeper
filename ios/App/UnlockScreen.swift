import SwiftUI
import SecretCore

/// 画板 10：移动端解锁。上半品牌区固定，下半白卡表单。
struct UnlockScreen: View {
    @EnvironmentObject var state: AppViewModel
    @State private var password = ""
    @State private var passwordVisible = false

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            // ---- 品牌区 ----
            VStack(alignment: .leading, spacing: 0) {
                Text("密匣")
                    .font(.system(size: 18, weight: .semibold))
                    .foregroundColor(.white)
                Spacer(minLength: 64)
                Image(systemName: "checkmark.shield")
                    .font(.system(size: 44))
                    .foregroundColor(SkColors.primarySoft)
                    .frame(maxWidth: .infinity)
                Text("SecretKeeper")
                    .font(.system(size: 13))
                    .foregroundColor(SkColors.textFaint)
                    .frame(maxWidth: .infinity)
                    .padding(.top, 6)
                Text("你的秘密，只在本机。")
                    .font(.system(size: 22, weight: .semibold))
                    .foregroundColor(.white)
                    .frame(maxWidth: .infinity)
                    .padding(.top, 8)
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 24)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(SkColors.brandBg)

            // ---- 白卡表单 ----
            VStack(alignment: .leading, spacing: 0) {
                HStack {
                    Text("解锁密匣").font(.system(size: 20, weight: .semibold))
                    Spacer()
                    Text(state.unlocked ? "已解锁" : "已锁定")
                        .font(.system(size: 12))
                        .foregroundColor(SkColors.textSub)
                }
                .padding(.bottom, 20)

                Text("主密钥").font(.system(size: 13))
                KeySelectorCard()
                    .padding(.vertical, 8)

                Text("主密钥密码").font(.system(size: 13))
                HStack {
                    Group {
                        if passwordVisible {
                            TextField("输入主密钥密码", text: $password)
                        } else {
                            SecureField("输入主密钥密码", text: $password)
                        }
                    }
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    Button {
                        passwordVisible.toggle()
                    } label: {
                        Image(systemName: passwordVisible ? "eye.slash" : "eye")
                            .foregroundColor(SkColors.textSub)
                    }
                }
                .padding(.vertical, 8)

                if let msg = state.unlockError {
                    HStack(spacing: 8) {
                        Text("⚠").font(.system(size: 13))
                        Text(msg).font(.system(size: 13))
                    }
                    .foregroundColor(SkColors.dangerText)
                    .padding(12)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(SkColors.dangerBg)
                    .cornerRadius(8)
                    .padding(.vertical, 12)
                }

                let waiting = state.backoff.waiting && state.backoff.remainingSeconds > 0
                Button {
                    let id = state.defaultKeyId.isEmpty
                        ? state.masterKeys.first?.masterKeyId ?? ""
                        : state.defaultKeyId
                    state.unlock(id, password)
                    password.removeAll()
                } label: {
                    HStack {
                        if waiting {
                            Image(systemName: "lock.fill")
                            Text("\(state.backoff.remainingSeconds) 秒后可解锁")
                        } else {
                            Text("解锁")
                        }
                    }
                    .frame(maxWidth: .infinity)
                    .frame(height: 46)
                }
                .disabled(waiting || password.isEmpty)
                .background(waiting || password.isEmpty ? SkColors.pillBg : SkColors.primary)
                .foregroundColor(waiting || password.isEmpty ? SkColors.textSub : .white)
                .cornerRadius(8)
                .padding(.top, 12)

                Spacer()
                Text("密码丢失无法解密，没有在线找回方式。")
                    .font(.system(size: 12))
                    .foregroundColor(SkColors.textSub)
                Text("完全离线 · 无账户 · 无云同步 · v0.0.1")
                    .font(.system(size: 12))
                    .foregroundColor(SkColors.textFaint)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 10)
            }
            .padding(20)
            .background(
                UnevenRoundedRectangle(topLeadingRadius: 12,
                                       bottomLeadingRadius: 0,
                                       bottomTrailingRadius: 0,
                                       topTrailingRadius: 12)
                    .fill(SkColors.cardBg)
                    .ignoresSafeArea(edges: .bottom)
            )
        }
        .background(SkColors.brandBg.ignoresSafeArea())
    }

    /// 主密钥选择器卡：钥匙 + 名称/ID 两行 + 「默认」徽章。
    @ViewBuilder
    private func KeySelectorCard() -> some View {
        let name = state.defaultKeyName.isEmpty
            ? state.masterKeys.first?.name ?? ""
            : state.defaultKeyName
        let id = state.defaultKeyId.isEmpty
            ? state.masterKeys.first?.masterKeyId ?? ""
            : state.defaultKeyId
        HStack(spacing: 12) {
            Image(systemName: "key.fill").foregroundColor(SkColors.primary)
            VStack(alignment: .leading, spacing: 2) {
                Text(name.isEmpty ? "主密钥" : name)
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundColor(SkColors.textMain)
                Text(shortId(id))
                    .font(.system(size: 12))
                    .foregroundColor(SkColors.textSub)
            }
            Spacer()
            Text("默认")
                .font(.system(size: 12))
                .foregroundColor(SkColors.primary)
                .padding(.horizontal, 8).padding(.vertical, 3)
                .background(SkColors.primaryWash)
                .cornerRadius(6)
        }
        .padding(14)
        .background(SkColors.pageBg)
        .cornerRadius(8)
    }
}
