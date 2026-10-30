package com.secretkeeper.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import com.secretkeeper.app.ui.AppViewModel
import com.secretkeeper.app.ui.MainScaffold
import com.secretkeeper.app.ui.SkTab
import com.secretkeeper.app.ui.SkTheme
import com.secretkeeper.app.ui.UnlockScreen

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // FLAG_SECURE：最近任务与截屏不显示内容（离线机密应用的基本防护）
        window.setFlags(
            android.view.WindowManager.LayoutParams.FLAG_SECURE,
            android.view.WindowManager.LayoutParams.FLAG_SECURE,
        )
        setContent {
            SkTheme {
                // 数据目录：/data/data/<pkg>/files（docs/03-data §6.1 的 Android 行）
                val state = remember { AppViewModel(getExternalFilesDir(null) ?: filesDir) }
                var tab by remember { mutableStateOf(SkTab.SECRETS) }
                if (state.unlocked) {
                    MainScaffold(
                        state = state,
                        tab = tab,
                        onTabChange = {
                            tab = it
                            state.notifyActivity()
                        },
                        onLock = state::lock,
                    )
                } else {
                    UnlockScreen(state)
                }
            }
        }
    }
}
