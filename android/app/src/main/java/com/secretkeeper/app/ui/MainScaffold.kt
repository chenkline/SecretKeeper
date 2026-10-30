package com.secretkeeper.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.sp

/**
 * 底部导航三页（画板 11/14/20 的底部栏）。
 * 图标用 Emoji 占位（v0.0.1 不引入 material-icons-extended 大依赖，
 * TODO(H3)：换自绘矢量图标与桌面端 draw_icon 同源）。
 */
enum class SkTab(val label: String, val glyph: String) {
    SECRETS("机密信息", "🔐"),
    KEYS("主密钥", "🔑"),
    SETTINGS("安全设置", "⚙"),
}

/** 主界面骨架：顶部内容区 + 底部三 tab 导航（锁定时整页回解锁屏）。 */
@Composable
fun MainScaffold(
    state: AppViewModel,
    tab: SkTab,
    onTabChange: (SkTab) -> Unit,
    onLock: () -> Unit,
    onOpenForm: (SkForm) -> Unit = {},
    onOpenDetail: (String) -> Unit = {},
) {
    Scaffold(
        containerColor = SkColors.PageBg,
        bottomBar = {
            NavigationBar(containerColor = SkColors.CardBg) {
                SkTab.entries.forEach { t ->
                    NavigationBarItem(
                        selected = t == tab,
                        onClick = { onTabChange(t) },
                        icon = { Text(t.glyph, fontSize = 18.sp) },
                        label = { Text(t.label, fontSize = 12.sp) },
                        colors = NavigationBarItemDefaults.colors(
                            selectedIconColor = SkColors.Primary,
                            selectedTextColor = SkColors.Primary,
                            unselectedIconColor = SkColors.TextSub,
                            unselectedTextColor = SkColors.TextSub,
                            indicatorColor = SkColors.PrimaryWash,
                        ),
                    )
                }
            }
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(SkColors.PageBg)
                .padding(padding),
        ) {
            when (tab) {
                SkTab.SECRETS -> SecretsScreen(
                    state,
                    onOpenAddSecret = { onOpenForm(SkForm.ADD_SECRET) },
                    onOpenImportSecret = { onOpenForm(SkForm.IMPORT_SECRET) },
                    onOpenDetail = onOpenDetail,
                )
                SkTab.KEYS -> KeysScreen(
                    state,
                    onOpenCreateKey = { onOpenForm(SkForm.CREATE_KEY) },
                    onOpenImportKey = { onOpenForm(SkForm.IMPORT_KEY) },
                )
                SkTab.SETTINGS -> SettingsScreen(state, onLock)
            }
        }
    }
}
