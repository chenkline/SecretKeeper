package com.secretkeeper.app.ui

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/**
 * 色值取自 docs/ui/UI设计规范.md §1（设计稿实测），与桌面端 theme.h 同源。
 */
object SkColors {
    val BrandBg = Color(0xFF1A3B5D) // 品牌 navy 底（v3 Bitwarden）
    val BrandBgDeep = Color(0xFF1D2C31) // 标题深青
    val Primary = Color(0xFF175DDC) // 主蓝（v3 Bitwarden #175DDC）
    val PrimarySoft = Color(0xFF3B6EA5) // 中蓝（v3 行首圆底图标）
    val PrimaryWash = Color(0xFFEEF2F7) // 选中行淡蓝灰底（v3）
    val PageBg = Color(0xFFF5F7F9) // 页面蓝灰底（v3）
    val CardBg = Color(0xFFFFFFFF)
    val TextMain = Color(0xFF212529)
    val TextSub = Color(0xFF718087) // 次要文本
    val TextFaint = Color(0xFF96AFB3) // 占位文本
    val Border = Color(0xFFDFE5E7)
    val WarnBg = Color(0xFFFFF7E3) // 提示条淡黄底
    val WarnText = Color(0xFFB58318)
    val DangerBg = Color(0xFFFCF0EF) // 错误条淡红底
    val DangerText = Color(0xFFB24747)
    val DangerSoftBg = Color(0xFFF8E4E4) // 删除按钮淡红底
    val PillBg = Color(0xFFE8EDEE)
    val CategoryIconBg = Color(0xFFEEF2F7) // v3 行首圆底分类图标
    val CategoryIconFg = Color(0xFF3B6EA5) // v3 圆底图标中蓝
    val Navy = Color(0xFF1A3B5D) // v3 底导航 navy
}

private val LightScheme = lightColorScheme(
    primary = SkColors.Primary,
    onPrimary = Color.White,
    background = SkColors.PageBg,
    onBackground = SkColors.TextMain,
    surface = SkColors.CardBg,
    onSurface = SkColors.TextMain,
    surfaceVariant = SkColors.PillBg,
    error = SkColors.DangerText,
)

/** 卡片圆角统一 12dp（画板 rx=12）。 */
val SkCardShape = RoundedCornerShape(12.dp)

@Composable
fun SkTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = LightScheme, content = content)
}
