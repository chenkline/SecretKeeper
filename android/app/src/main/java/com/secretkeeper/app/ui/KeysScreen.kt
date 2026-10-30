package com.secretkeeper.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.secretkeeper.app.core.MasterKeyListItem

/**
 * 画板 11：主密钥管理。生成/导入入口、密钥卡（默认徽章、导出/删除/设为默认）、
 * 配额提示条（限额是业务规则：2 把，达上限时黄色提示条）。
 */
@Composable
fun KeysScreen(state: AppViewModel, onOpenCreateKey: () -> Unit = {}) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(20.dp),
    ) {
        Text(
            text = "主密钥管理",
            color = SkColors.TextMain,
            fontSize = 22.sp,
            fontWeight = FontWeight.SemiBold,
        )
        Spacer(Modifier.height(16.dp))

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OutlinedButton(
                onClick = onOpenCreateKey,
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(8.dp),
            ) { Text("生成主密钥") }
            OutlinedButton(
                onClick = { /* TODO(H2): 导入主密钥表单（画板 13） */ },
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(8.dp),
            ) { Text("📥 导入主密钥") }
        }
        Spacer(Modifier.height(18.dp))

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = "本机保存的主密钥",
                color = SkColors.TextSub,
                fontSize = 13.sp,
                modifier = Modifier.weight(1f),
            )
            Text(
                text = "${state.masterKeys.size} / 2 把",
                color = SkColors.WarnText,
                fontSize = 13.sp,
            )
        }
        Spacer(Modifier.height(10.dp))

        state.masterKeys.forEach { item ->
            MasterKeyCard(item, isDefaultLocked = item.isDefault)
            Spacer(Modifier.height(12.dp))
        }

        // 配额达上限提示条（业务规则在 service，UI 如实展示固定文案）
        if (state.masterKeys.size >= 2) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(SkColors.WarnBg, RoundedCornerShape(8.dp))
                    .padding(12.dp),
            ) {
                Text(
                    text = "⚠ 主密钥数量已满，先导出并删除一把主密钥，才能新增。",
                    color = SkColors.WarnText,
                    fontSize = 13.sp,
                )
            }
        }
        Spacer(Modifier.height(24.dp))
    }
}

@Composable
private fun MasterKeyCard(item: MasterKeyListItem, isDefaultLocked: Boolean) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(SkColors.CardBg, SkCardShape)
            .padding(16.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(text = "🔑", fontSize = 15.sp)
            Spacer(Modifier.width(8.dp))
            Text(
                text = item.name.ifEmpty { shortId(item.masterKeyId) },
                color = SkColors.TextMain,
                fontSize = 16.sp,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.weight(1f),
            )
            if (item.isDefault) {
                Text(
                    text = "默认",
                    color = SkColors.Primary,
                    fontSize = 12.sp,
                    modifier = Modifier
                        .background(SkColors.PrimaryWash, RoundedCornerShape(6.dp))
                        .padding(horizontal = 8.dp, vertical = 3.dp),
                )
            }
        }
        Spacer(Modifier.height(12.dp))

        Row {
            Text(
                text = "主密钥 ID",
                color = SkColors.TextSub,
                fontSize = 13.sp,
                modifier = Modifier.width(80.dp),
            )
            Text(text = shortId(item.masterKeyId), color = SkColors.TextMain, fontSize = 13.sp)
        }
        Spacer(Modifier.height(6.dp))
        Text(
            text = "RSA-2048 · 关联 ${item.secretCount} 条机密信息",
            color = SkColors.TextSub,
            fontSize = 13.sp,
        )
        Spacer(Modifier.height(12.dp))

        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            OutlinedButton(
                onClick = { /* TODO(H2): 导出主密钥（画板 19/05） */ },
                shape = RoundedCornerShape(8.dp),
            ) { Text("导出主密钥") }
            Button(
                onClick = { /* TODO(H2): 设为默认（switchDefaultMasterKey） */ },
                enabled = !item.isDefault,
                shape = RoundedCornerShape(8.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = SkColors.Primary,
                    disabledContainerColor = SkColors.PrimaryWash,
                ),
            ) { Text("设为默认") }
            TextButton(
                onClick = { /* TODO(H2): 删除确认（默认主密钥禁止删除） */ },
                enabled = !item.isDefault,
                shape = RoundedCornerShape(8.dp),
                colors = ButtonDefaults.textButtonColors(
                    contentColor = SkColors.DangerText,
                    disabledContentColor = SkColors.TextFaint,
                ),
            ) { Text("删除") }
        }

        if (item.isDefault) {
            Spacer(Modifier.height(8.dp))
            Text(
                text = "默认主密钥禁止删除。请先切换默认。",
                color = SkColors.TextSub,
                fontSize = 12.sp,
            )
        }
    }
}
