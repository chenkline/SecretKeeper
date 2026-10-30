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
    val BrandBg = Color(0xFF172C31) // 品牌深青底（画板 10 上半区）
    val BrandBgDeep = Color(0xFF1D2C31) // 标题深青
    val Primary = Color(0xFF14786D) // 主青（按钮/选中态）
    val PrimarySoft = Color(0xFF68C3AD) // 亮青（图标/徽章）
    val PrimaryWash = Color(0xFFE8F4F0) // 选中行淡青底
    val PageBg = Color(0xFFF3F5F6) // 页面浅灰底
    val CardBg = Color(0xFFFFFFFF)
    val TextMain = Color(0xFF1D2C31)
    val TextSub = Color(0xFF718087) // 次要文本
    val TextFaint = Color(0xFF96AFB3) // 占位文本
    val Border = Color(0xFFDFE5E7)
    val WarnBg = Color(0xFFFFF7E3) // 提示条淡黄底
    val WarnText = Color(0xFFB58318)
    val DangerBg = Color(0xFFFCF0EF) // 错误条淡红底
    val DangerText = Color(0xFFB24747)
    val DangerSoftBg = Color(0xFFF8E4E4) // 删除按钮淡红底
    val PillBg = Color(0xFFE8EDEE)
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
