package com.secretkeeper.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * 画板 20：安全设置。锁定操作、离线声明、版本信息。
 * 闲置锁定时长等策略项由 app_meta 承载（v0.0.1 固定值）。
 */
@Composable
fun SettingsScreen(state: AppViewModel, onLock: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(20.dp),
    ) {
        Text(
            text = "安全设置",
            color = SkColors.TextMain,
            fontSize = 22.sp,
            fontWeight = FontWeight.SemiBold,
        )
        Spacer(Modifier.height(16.dp))

        SettingCard {
            Text("锁定", color = SkColors.TextMain, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(8.dp))
            Text(
                text = "立即清空内存中的密钥缓存。闲置 30 秒自动轮换密钥缓存（需求 2.7）。",
                color = SkColors.TextSub,
                fontSize = 13.sp,
            )
            Spacer(Modifier.height(12.dp))
            Button(
                onClick = onLock,
                enabled = state.unlocked,
                shape = RoundedCornerShape(8.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = SkColors.Primary,
                    disabledContainerColor = SkColors.PillBg,
                ),
            ) { Text("立即锁定") }
        }

        Spacer(Modifier.height(12.dp))

        SettingCard {
            Text("数据与隐私", color = SkColors.TextMain, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(8.dp))
            SettingLine("数据位置", "本机私有目录，不联网")
            SettingLine("密钥派生", "Argon2id（10 MiB / 3 / 1）")
            SettingLine("机密加密", "AES-256-GCM（每条独立密钥）")
            SettingLine("主密钥", "RSA-2048（OAEP-SHA256）")
        }

        Spacer(Modifier.height(12.dp))

        SettingCard {
            Text("关于", color = SkColors.TextMain, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(8.dp))
            SettingLine("版本", "v0.0.1（基础版：2 把主密钥 / 15 条机密 / 150 字符）")
            SettingLine("离线承诺", "无账户 · 无云同步 · 无遥测")
        }
        Spacer(Modifier.height(24.dp))
    }
}

@Composable
private fun SettingCard(content: @Composable () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(SkColors.CardBg, SkCardShape)
            .padding(16.dp),
    ) {
        content()
    }
}

@Composable
private fun SettingLine(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
        Text(
            text = label,
            color = SkColors.TextSub,
            fontSize = 13.sp,
            modifier = Modifier.weight(1f),
        )
        Text(text = value, color = SkColors.TextMain, fontSize = 13.sp)
    }
}
