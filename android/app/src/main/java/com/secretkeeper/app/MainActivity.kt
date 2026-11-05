package com.secretkeeper.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import com.secretkeeper.app.ui.AppViewModel
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import com.secretkeeper.app.ui.DetailScreen
import com.secretkeeper.app.ui.FormScaffold
import com.secretkeeper.app.ui.MainScaffold
import com.secretkeeper.app.ui.SkForm
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
                var form by remember { mutableStateOf<SkForm?>(null) }
                var detailId by remember { mutableStateOf<String?>(null) }
                // 详情页导出：确认后由 SAF CreateDocument 落盘（需求 3.3 逐字节导出）
                val secretExportLauncher = rememberLauncherForActivityResult(
                    ActivityResultContracts.CreateDocument("application/octet-stream"),
                ) { uri ->
                    val sid = state.requestSecretExport
                    if (uri != null && sid != null) {
                        val (e, bytes) = state.exportSecret(sid)
                        if (e == com.secretkeeper.app.core.SvcError.OK && bytes != null) {
                            contentResolver.openOutputStream(uri)?.use { it.write(bytes) }
                        }
                    }
                    state.requestSecretExport = null
                }
                androidx.compose.runtime.LaunchedEffect(state.requestSecretExport) {
                    if (state.requestSecretExport != null) {
                        secretExportLauncher.launch("secret-" +
                            state.requestSecretExport!!.takeLast(8) + ".sscexp")
                    }
                }
                if (state.unlocked && detailId != null) {
                    DetailScreen(state, detailId!!, onBack = { detailId = null })
                } else if (state.unlocked && form != null) {
                    FormScaffold(state, form!!, onBack = { form = null })
                } else if (state.unlocked) {
                    MainScaffold(
                        state = state,
                        tab = tab,
                        onTabChange = {
                            tab = it
                            state.notifyActivity()
                        },
                        onLock = state::lock,
                        onOpenForm = { form = it },
                        onOpenDetail = { id ->
                            detailId = id
                            state.loadDetail(id)
                        },
                    )
                } else {
                    UnlockScreen(state)
                }

                // 需求 2.7：30 秒 KEK 轮换定时器（解锁期间生效）。
                LaunchedEffect(state.unlocked) {
                    while (state.unlocked) {
                        kotlinx.coroutines.delay(30_000L)
                        state.rotateKekTick()
                    }
                }

                // 需求 4.1：闲置 5 分钟自动锁定（任何交互由 notifyActivity 重置）。
                LaunchedEffect(state.unlocked) {
                    val idleLimitMs = 5 * 60 * 1000L
                    while (state.unlocked) {
                        kotlinx.coroutines.delay(30_000L)  // 30 秒检查周期
                        if (state.idleMs() >= idleLimitMs) {
                            state.lock()
                        }
                    }
                }
            }
        }
    }
}
