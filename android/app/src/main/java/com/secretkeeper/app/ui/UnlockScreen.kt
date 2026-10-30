package com.secretkeeper.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.secretkeeper.app.core.ServiceLimits

/**
 * 画板 10：移动端解锁。上半品牌区固定，下半白卡表单。
 * 密码输入禁用自动纠错/自动大写（AGENTS.md §8），掩码显示。
 */
@Composable
fun UnlockScreen(
    state: AppViewModel,
    modifier: Modifier = Modifier,
) {
    var password by remember { mutableStateOf(charArrayOf()) }
    var passwordVisible by remember { mutableStateOf(false) }

    Column(modifier = modifier.fillMaxSize().background(SkColors.BrandBg)) {
        // ---- 品牌区（固定） ----
        Column(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 24.dp),
        ) {
            Text(
                text = "密匣",
                color = Color.White,
                fontSize = 18.sp,
                fontWeight = FontWeight.SemiBold,
            )
            Spacer(Modifier.height(72.dp))
            Icon(
                imageVector = SkIcons.ShieldCheck,
                contentDescription = null,
                tint = SkColors.PrimarySoft,
                modifier = Modifier
                    .size(60.dp)
                    .align(Alignment.CenterHorizontally),
            )
            Spacer(Modifier.height(12.dp))
            Text(
                text = "SecretKeeper",
                color = SkColors.TextFaint,
                fontSize = 13.sp,
                modifier = Modifier.align(Alignment.CenterHorizontally),
            )
            Spacer(Modifier.height(8.dp))
            Text(
                text = "你的秘密，只在本机。",
                color = Color.White,
                fontSize = 22.sp,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.align(Alignment.CenterHorizontally),
            )
            Spacer(Modifier.height(24.dp))
        }

        // ---- 白卡表单 ----
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .background(
                    SkColors.CardBg,
                    RoundedCornerShape(topStart = 12.dp, topEnd = 12.dp),
                )
                .padding(20.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = "解锁密匣",
                    color = SkColors.TextMain,
                    fontSize = 20.sp,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.weight(1f),
                )
                Text(
                    text = if (state.unlocked) "已解锁" else "已锁定",
                    color = SkColors.TextSub,
                    fontSize = 12.sp,
                )
            }
            Spacer(Modifier.height(20.dp))

            Text(text = "主密钥", color = SkColors.TextMain, fontSize = 13.sp)
            Spacer(Modifier.height(8.dp))
            KeySelectorCard(state)
            Spacer(Modifier.height(16.dp))

            Text(text = "主密钥密码", color = SkColors.TextMain, fontSize = 13.sp)
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(
                value = password.concatToString(),
                onValueChange = { password = it.toCharArray() },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                placeholder = { Text("输入主密钥密码", color = SkColors.TextFaint, fontSize = 14.sp) },
                visualTransformation = if (passwordVisible) {
                    VisualTransformation.None
                } else {
                    PasswordVisualTransformation()
                },
                // 密码键盘自带掩码与免自动纠错；显式关大写（AGENTS.md §8）
                keyboardOptions = KeyboardOptions(
                    keyboardType = KeyboardType.Password,
                    capitalization = KeyboardCapitalization.None,
                ),
                trailingIcon = {
                    IconButton(onClick = { passwordVisible = !passwordVisible }) {
                        Icon(
                            imageVector = if (passwordVisible) SkIcons.EyeOff else SkIcons.Eye,
                            contentDescription = null,
                            tint = SkColors.TextSub,
                            modifier = Modifier.size(20.dp),
                        )
                    }
                },
                colors = OutlinedTextFieldDefaults.colors(
                    focusedBorderColor = SkColors.Primary,
                    unfocusedBorderColor = SkColors.Border,
                ),
                shape = RoundedCornerShape(8.dp),
            )

            // ---- 错误条（需求固定文案） ----
            state.unlockError?.let { msg ->
                Spacer(Modifier.height(14.dp))
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(SkColors.DangerBg, RoundedCornerShape(8.dp))
                        .padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(text = "⚠", color = SkColors.DangerText, fontSize = 13.sp)
                    Spacer(Modifier.width(8.dp))
                    Text(text = msg, color = SkColors.DangerText, fontSize = 13.sp)
                }
            }

            Spacer(Modifier.height(14.dp))

            // ---- 解锁按钮：退避窗口内禁用并显示剩余秒数（需求 4.5） ----
            val waiting = state.backoff.waiting && state.backoff.remainingSeconds > 0
            Button(
                onClick = {
                    state.unlock(state.defaultKeyId.ifEmpty {
                        state.masterKeys.firstOrNull()?.masterKeyId ?: ""
                    }, password)
                    password.fill(0.toChar()) // 密码缓冲用后清零
                },
                enabled = !waiting && password.isNotEmpty(),
                modifier = Modifier.fillMaxWidth().height(46.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (waiting) SkColors.PillBg else SkColors.Primary,
                    contentColor = if (waiting) SkColors.TextSub else Color.White,
                    disabledContainerColor = SkColors.PillBg,
                    disabledContentColor = SkColors.TextSub,
                ),
                shape = RoundedCornerShape(8.dp),
            ) {
                if (waiting) {
                    Icon(
                        imageVector = Icons.Filled.Lock,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp),
                    )
                    Spacer(Modifier.width(8.dp))
                    Text("${state.backoff.remainingSeconds} 秒后可解锁")
                } else {
                    Text("解锁")
                }
            }

            Spacer(Modifier.weight(1f))
            Text(
                text = "密码丢失无法解密，没有在线找回方式。",
                color = SkColors.TextSub,
                fontSize = 12.sp,
            )
            Spacer(Modifier.height(10.dp))
            Text(
                text = "完全离线 · 无账户 · 无云同步 · v0.0.1",
                color = SkColors.TextFaint,
                fontSize = 12.sp,
                modifier = Modifier.align(Alignment.CenterHorizontally),
            )
            Spacer(Modifier.height(12.dp))
        }
    }
}

/** 主密钥选择器卡：图标 + 名称/ID 两行 + 「默认」徽章 + 下拉箭头（v0.0.1 单选默认密钥）。 */
@Composable
private fun KeySelectorCard(state: AppViewModel) {
    val name = state.defaultKeyName.ifEmpty { state.masterKeys.firstOrNull()?.name ?: "" }
    val id = state.defaultKeyId.ifEmpty { state.masterKeys.firstOrNull()?.masterKeyId ?: "" }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(SkColors.PageBg, RoundedCornerShape(8.dp))
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text = "🔑", fontSize = 16.sp)
        Spacer(Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(text = name.ifEmpty { "主密钥" }, color = SkColors.TextMain, fontSize = 15.sp,
                fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(2.dp))
            Text(
                text = shortId(id),
                color = SkColors.TextSub,
                fontSize = 12.sp,
            )
        }
        Text(
            text = "默认",
            color = SkColors.Primary,
            fontSize = 12.sp,
            modifier = Modifier
                .background(SkColors.PrimaryWash, RoundedCornerShape(6.dp))
                .padding(horizontal = 8.dp, vertical = 3.dp),
        )
        Spacer(Modifier.width(6.dp))
        Text(text = "⌄", color = SkColors.TextSub, fontSize = 14.sp)
    }
}

/** 32 位十六进制 ID 折叠为 MK-XXXX 短格式（列表展示约定）。 */
internal fun shortId(hex: String): String =
    if (hex.length >= 8) "MK-" + hex.substring(0, 4).uppercase() else hex
