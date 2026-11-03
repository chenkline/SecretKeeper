import SwiftUI

/// 色值取自 docs/ui/UI设计规范.md §1（与桌面 theme.h、Android SkColors 同源）。
enum SkColors {
    static let brandBg = Color(red: 0x17/255, green: 0x2C/255, blue: 0x31/255)
    static let primary = Color(red: 0x14/255, green: 0x78/255, blue: 0x6D/255)
    static let primarySoft = Color(red: 0x68/255, green: 0xC3/255, blue: 0xAD/255)
    static let primaryWash = Color(red: 0xE8/255, green: 0xF4/255, blue: 0xF0/255)
    static let pageBg = Color(red: 0xF3/255, green: 0xF5/255, blue: 0xF6/255)
    static let cardBg = Color.white
    static let textMain = Color(red: 0x1D/255, green: 0x2C/255, blue: 0x31/255)
    static let textSub = Color(red: 0x71/255, green: 0x80/255, blue: 0x87/255)
    static let textFaint = Color(red: 0x96/255, green: 0xAF/255, blue: 0xB3/255)
    static let border = Color(red: 0xDF/255, green: 0xE5/255, blue: 0xE7/255)
    static let warnBg = Color(red: 0xFF/255, green: 0xF7/255, blue: 0xE3/255)
    static let warnText = Color(red: 0xB5/255, green: 0x83/255, blue: 0x18/255)
    static let dangerBg = Color(red: 0xFC/255, green: 0xF0/255, blue: 0xEF/255)
    static let dangerText = Color(red: 0xB2/255, green: 0x47/255, blue: 0x47/255)
    static let pillBg = Color(red: 0xE8/255, green: 0xED/255, blue: 0xEE/255)
}

/// 32 位十六进制 ID 折叠为 MK-XXXX 短格式（列表展示约定）。
func shortId(_ hex: String) -> String {
    hex.count >= 8 ? "MK-" + hex.prefix(4).uppercased() : hex
}

/// 白卡容器（圆角 12）。
struct SkCard<Content: View>: View {
    let content: Content

    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    var body: some View {
        content
            .padding(16)
            .background(SkColors.cardBg)
            .cornerRadius(12)
    }
}
