package com.secretkeeper.app.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathFillType
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.path
import androidx.compose.ui.unit.dp

/**
 * 自绘矢量图标：与桌面端 widgets.cpp 的 draw_icon 同一套图形语言
 * （24dp 视口、2dp 圆头描边、线性风格）。v0.0.1 不引入
 * material-icons-extended 大依赖，图标自绘保证三端视觉同源。
 */
object SkIcons {

    /** 盾牌 + 对勾（解锁页品牌图标，画板 10）。 */
    val ShieldCheck: ImageVector by lazy {
        ImageVector.Builder(
            name = "SkShieldCheck",
            defaultWidth = 24.dp, defaultHeight = 24.dp,
            viewportWidth = 24f, viewportHeight = 24f,
        ).apply {
            path(
                fill = null, stroke = SolidColor(Color(0xFF68C3AD)),
                strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round,
                strokeLineJoin = StrokeJoin.Round,
                pathFillType = PathFillType.NonZero,
            ) {
                // 盾形：顶边水平，两侧下收，底部汇聚
                moveTo(12f, 2.5f)
                lineTo(20f, 5.5f)
                verticalLineTo(11f)
                curveTo(20f, 16.2f, 16.8f, 19.9f, 12f, 21.5f)
                curveTo(7.2f, 19.9f, 4f, 16.2f, 4f, 11f)
                verticalLineTo(5.5f)
                close()
            }
            path(
                fill = null, stroke = SolidColor(Color(0xFF68C3AD)),
                strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round,
                strokeLineJoin = StrokeJoin.Round,
            ) {
                // 对勾
                moveTo(8.5f, 11.5f)
                lineTo(11f, 14f)
                lineTo(15.5f, 9f)
            }
        }.build()
    }

    /** 钥匙（tab / 密钥卡 / 表单，画板 11/17）。 */
    val Key: ImageVector by lazy {
        ImageVector.Builder(
            name = "SkKey",
            defaultWidth = 24.dp, defaultHeight = 24.dp,
            viewportWidth = 24f, viewportHeight = 24f,
        ).apply {
            path(
                fill = null, stroke = SolidColor(Color(0xFF14786D)),
                strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round,
            ) {
                // 钥匙环
                moveTo(18.7f, 9.5f)
                arcToRelative(4.2f, 4.2f, 0f, true, true, -8.4f, 0f)
                arcToRelative(4.2f, 4.2f, 0f, true, true, 8.4f, 0f)
                close()
                // 柄（斜向左下）
                moveTo(11.3f, 12.7f)
                lineTo(4f, 20f)
                // 齿
                moveTo(6.8f, 17.2f)
                lineTo(9.3f, 19.7f)
                moveTo(8.4f, 15.6f)
                lineTo(10.9f, 18.1f)
            }
        }.build()
    }

    /** 挂锁（机密信息 tab / 已锁定态，画板 14/10）。 */
    val Lock: ImageVector by lazy {
        ImageVector.Builder(
            name = "SkLock",
            defaultWidth = 24.dp, defaultHeight = 24.dp,
            viewportWidth = 24f, viewportHeight = 24f,
        ).apply {
            path(
                fill = null, stroke = SolidColor(Color(0xFF14786D)),
                strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round,
            ) {
                // 锁体
                moveTo(6f, 11f)
                horizontalLineTo(18f)
                verticalLineTo(19.5f)
                arcToRelative(0.5f, 0.5f, 0f, false, true, -0.5f, 0.5f)
                horizontalLineTo(6.5f)
                arcToRelative(0.5f, 0.5f, 0f, false, true, -0.5f, -0.5f)
                close()
                // 锁梁
                moveTo(8.5f, 11f)
                verticalLineTo(8f)
                arcToRelative(3.5f, 3.5f, 0f, false, true, 7f, 0f)
                verticalLineTo(11f)
            }
        }.build()
    }

    /** 齿轮（安全设置 tab，画板 20）。 */
    val Gear: ImageVector by lazy {
        ImageVector.Builder(
            name = "SkGear",
            defaultWidth = 24.dp, defaultHeight = 24.dp,
            viewportWidth = 24f, viewportHeight = 24f,
        ).apply {
            path(
                fill = null, stroke = SolidColor(Color(0xFF718087)),
                strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round,
                strokeLineJoin = StrokeJoin.Round,
            ) {
                // 齿圈：八向短线（每条命令独立一行，PathBuilder 为 infix 风格）
                moveTo(12f, 2.8f); verticalLineTo(5f)
                moveTo(12f, 19f); verticalLineTo(21.2f)
                moveTo(2.8f, 12f); horizontalLineTo(5f)
                moveTo(19f, 12f); horizontalLineTo(21.2f)
                moveTo(5.5f, 5.5f); lineTo(7f, 7f)
                moveTo(17f, 17f); lineTo(18.5f, 18.5f)
                moveTo(18.5f, 5.5f); lineTo(17f, 7f)
                moveTo(7f, 17f); lineTo(5.5f, 18.5f)
            }
            path(
                fill = null, stroke = SolidColor(Color(0xFF718087)),
                strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round,
            ) {
                // 圆环主体
                moveTo(12f, 7.6f)
                arcToRelative(4.4f, 4.4f, 0f, true, true, 0.01f, 0f)
            }
        }.build()
    }

    /** 眼睛（密码可见切换）。 */
    val Eye: ImageVector by lazy {
        ImageVector.Builder(
            name = "SkEye",
            defaultWidth = 24.dp, defaultHeight = 24.dp,
            viewportWidth = 24f, viewportHeight = 24f,
        ).apply {
            path(
                fill = null, stroke = SolidColor(Color(0xFF718087)),
                strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round,
                strokeLineJoin = StrokeJoin.Round,
            ) {
                // 眼眶
                moveTo(2.5f, 12f)
                curveTo(5f, 7.5f, 8.3f, 5.5f, 12f, 5.5f)
                curveTo(15.7f, 5.5f, 19f, 7.5f, 21.5f, 12f)
                curveTo(19f, 16.5f, 15.7f, 18.5f, 12f, 18.5f)
                curveTo(8.3f, 18.5f, 5f, 16.5f, 2.5f, 12f)
                close()
                // 瞳孔
                moveTo(12f, 9.2f)
                arcToRelative(2.8f, 2.8f, 0f, true, true, 0.01f, 0f)
            }
        }.build()
    }

    /** 眼睛划线（密码隐藏切换）。 */
    val EyeOff: ImageVector by lazy {
        ImageVector.Builder(
            name = "SkEyeOff",
            defaultWidth = 24.dp, defaultHeight = 24.dp,
            viewportWidth = 24f, viewportHeight = 24f,
        ).apply {
            path(
                fill = null, stroke = SolidColor(Color(0xFF718087)),
                strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round,
                strokeLineJoin = StrokeJoin.Round,
            ) {
                // 半开眼眶
                moveTo(4.5f, 8.2f)
                curveTo(3.7f, 9.3f, 3f, 10.6f, 2.5f, 12f)
                curveTo(5f, 16.5f, 8.3f, 18.5f, 12f, 18.5f)
                curveTo(13.4f, 18.5f, 14.7f, 18.2f, 16f, 17.6f)
                moveTo(20f, 15.2f)
                curveTo(20.6f, 14.3f, 21.1f, 13.2f, 21.5f, 12f)
                curveTo(19f, 7.5f, 15.7f, 5.5f, 12f, 5.5f)
                curveTo(11.4f, 5.5f, 10.8f, 5.6f, 10.2f, 5.7f)
                // 瞳孔半环
                moveTo(12f, 9.2f)
                arcToRelative(2.8f, 2.8f, 0f, false, true, 2.8f, 2.8f)
                // 划线
                moveTo(4f, 20f)
                lineTo(20f, 4f)
            }
        }.build()
    }
}
