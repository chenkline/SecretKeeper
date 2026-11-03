import SwiftUI
import SecretCore

/// 画板 20：安全设置。立即锁定、数据与隐私、关于。
struct SettingsScreen: View {
    @EnvironmentObject var state: AppViewModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text("安全设置")
                    .font(.system(size: 22, weight: .semibold))
                    .padding(.bottom, 16)

                SkCard {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("锁定").font(.system(size: 16, weight: .semibold))
                        Text("立即清空内存中的密钥缓存。闲置 30 秒自动轮换密钥缓存（需求 2.7）。")
                            .font(.system(size: 13)).foregroundColor(SkColors.textSub)
                        Button("立即锁定") { state.lock() }
                            .buttonStyle(.borderedProminent)
                            .disabled(!state.unlocked)
                            .padding(.top, 6)
                    }
                }

                SkCard {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("数据与隐私").font(.system(size: 16, weight: .semibold))
                        SettingLine("数据位置", "本机私有目录，不联网")
                        SettingLine("密钥派生", "Argon2id（10 MiB / 3 / 1）")
                        SettingLine("机密加密", "AES-256-GCM（每条独立密钥）")
                        SettingLine("主密钥", "RSA-2048（OAEP-SHA256）")
                    }
                }

                SkCard {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("关于").font(.system(size: 16, weight: .semibold))
                        SettingLine("版本",
                                    "v0.0.1（基础版：2 把主密钥 / 15 条机密 / 150 字符）")
                        SettingLine("离线承诺", "无账户 · 无云同步 · 无遥测")
                    }
                }
            }
            .padding(20)
        }
        .background(SkColors.pageBg)
    }
}

private struct SettingLine: View {
    let label: String
    let value: String

    var body: some View {
        HStack(alignment: .top) {
            Text(label).font(.system(size: 13))
                .foregroundColor(SkColors.textSub)
            Spacer()
            Text(value).font(.system(size: 13))
                .multilineTextAlignment(.trailing)
        }
    }
}
