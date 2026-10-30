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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver

/**
 * 画板 15/16：机密信息详情。主密钥缺失 -> 掩码 + 黄问号（16）；存在 ->
 * 「查看」后显示明文，复制/导出可用；切换界面或进入后台立即隐藏（需求 3.5）。
 * 底部删除走确认对话框（画板 19）。
 */
@Composable
fun DetailScreen(
    state: AppViewModel,
    secretId: String,
    onBack: () -> Unit,
) {
    var showRevealDialog by remember { mutableStateOf(false) }
    var showDeleteDialog by remember { mutableStateOf(false) }
    var showExportConfirm by remember { mutableStateOf(false) }
    val clipboard = LocalClipboardManager.current

    // 需求 3.5：切换界面或进入后台，立即隐藏明文。
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_PAUSE) state.maskDetail()
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    val d = state.detail
    Column(modifier = Modifier.fillMaxSize().background(SkColors.PageBg)) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            IconButton(onClick = onBack) {
                Icon(
                    Icons.AutoMirrored.Filled.ArrowBack,
                    contentDescription = "返回",
                    tint = SkColors.TextMain,
                )
            }
            Text(
                text = "信息详情",
                color = SkColors.TextMain,
                fontSize = 17.sp,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.weight(1f),
            )
        }

        if (d == null) {
            Text(
                text = state.detailError ?: "条目不存在",
                color = SkColors.DangerText,
                fontSize = 13.sp,
                modifier = Modifier.padding(20.dp),
            )
            return@Column
        }

        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(20.dp),
        ) {
            // ---- 关联主密钥卡（缺失 -> 黄问号态，画板 16） ----
            Column(
                modifier = Modifier.fillMaxWidth().background(SkColors.CardBg, SkCardShape)
                    .padding(16.dp),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "关联主密钥",
                        color = SkColors.TextMain,
                        fontSize = 16.sp,
                        fontWeight = FontWeight.SemiBold,
                        modifier = Modifier.weight(1f),
                    )
                    Text(
                        text = if (d.masterKeyFound) "✓" else "？",
                        color = if (d.masterKeyFound) SkColors.Primary else SkColors.WarnText,
                        fontSize = 16.sp,
                    )
                }
                Spacer(Modifier.height(10.dp))
                DetailLine("主密钥 ID", shortId(d.masterKeyId) + if (d.masterKeyFound) "" else "?")
                DetailLine("主密钥名称", d.masterKeyName) // 空名保持留白
            }
            Spacer(Modifier.height(12.dp))

            // ---- 内容卡 ----
            Column(
                modifier = Modifier.fillMaxWidth().background(SkColors.CardBg, SkCardShape)
                    .padding(16.dp),
            ) {
                Row {
                    Text(
                        text = "标题",
                        color = SkColors.TextSub,
                        fontSize = 13.sp,
                        modifier = Modifier.width(64.dp),
                    )
                    Text(
                        text = d.title, // 可为空：留白
                        color = SkColors.TextMain,
                        fontSize = 14.sp,
                        modifier = Modifier.weight(1f),
                    )
                }
                Spacer(Modifier.height(12.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "机密信息内容",
                        color = SkColors.TextMain,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.SemiBold,
                        modifier = Modifier.weight(1f),
                    )
                    if (!d.plaintextMasked) {
                        TextButton(onClick = { state.maskDetail() }) {
                            androidx.compose.material3.Icon(
                                SkIcons.EyeOff,
                                contentDescription = null,
                                tint = SkColors.TextSub,
                                modifier = Modifier.width(16.dp),
                            )
                            Spacer(Modifier.width(4.dp))
                            Text("隐藏", color = SkColors.TextSub, fontSize = 12.sp)
                        }
                    }
                }
                // 掩码固定 6 个星号（需求 3.5 的 kMaskedPlaintext），不显示列表占位符
                val body = if (d.plaintextMasked) ServiceMaskText else d.plaintext
                Column(
                    modifier = Modifier.fillMaxWidth()
                        .background(SkColors.PageBg, RoundedCornerShape(8.dp))
                        .padding(14.dp),
                ) {
                    Text(
                        text = body,
                        color = if (d.plaintextMasked) SkColors.TextSub else SkColors.TextMain,
                        fontSize = 14.sp,
                    )
                }
                Spacer(Modifier.height(8.dp))
                Text(
                    text = "虚机构测数据 · 今日添加", // 列表页时间戳 v0.0.1 暂不回显（见 H3 说明）
                    color = SkColors.TextFaint,
                    fontSize = 11.sp,
                )
                Spacer(Modifier.height(12.dp))

                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Button(
                        onClick = { clipboard.setText(AnnotatedString(d.plaintext)) },
                        enabled = !d.plaintextMasked,
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(8.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = SkColors.Primary,
                            disabledContainerColor = SkColors.PillBg,
                        ),
                    ) { Text("复制") }
                    OutlinedButton(
                        onClick = { showExportConfirm = true },
                        enabled = !d.plaintextMasked,
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(8.dp),
                    ) { Text("导出") }
                }
            }

            Spacer(Modifier.height(8.dp))
            // 需求固定提示（画板 15 底部青条）
            Row(
                modifier = Modifier.fillMaxWidth()
                    .background(SkColors.PrimaryWash, RoundedCornerShape(8.dp))
                    .padding(12.dp),
            ) {
                Text(
                    text = "默认掩码，查看后才显示。切换界面或进入后台，立即隐藏明文。",
                    color = SkColors.Primary,
                    fontSize = 12.sp,
                )
            }
            Spacer(Modifier.height(10.dp))

            if (!d.masterKeyFound) {
                Text(
                    text = "主密钥缺失，查看或复制前需验证对应密码。",
                    color = SkColors.WarnText,
                    fontSize = 12.sp,
                )
            }
            Text(
                text = "主密钥的信息，查看或复制前需验证对应密码。",
                color = SkColors.TextSub,
                fontSize = 12.sp,
            )

            state.detailError?.let { msg ->
                Spacer(Modifier.height(8.dp))
                Text(text = msg, color = SkColors.DangerText, fontSize = 13.sp)
            }

            Spacer(Modifier.height(14.dp))
            // 删除入口（画板 15 底部淡红条 -> 画板 19 确认对话框）
            OutlinedButton(
                onClick = { showDeleteDialog = true },
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(8.dp),
                colors = ButtonDefaults.outlinedButtonColors(
                    contentColor = SkColors.DangerText,
                ),
            ) { Text("删除机密信息") }
        }
    }

    // ---- 查看密码弹层 ----
    if (showRevealDialog) {
        RevealPasswordDialog(
            keyName = (d?.masterKeyName ?: "").ifEmpty { shortId(d?.masterKeyId ?: "") },
            needsPassword = !((d?.masterKeyFound ?: false) &&
                state.isDefaultKey(d?.masterKeyId ?: "")),
            onConfirm = { pw ->
                state.revealDetail(secretId, pw)
                showRevealDialog = false
            },
            onDismiss = { showRevealDialog = false },
        )
    }

    // ---- 删除确认（画板 19） ----
    if (showDeleteDialog) {
        AlertDialog(
            onDismissRequest = { showDeleteDialog = false },
            title = { Text("删除机密信息") },
            text = {
                Text("删除后无法恢复。建议先导出备份。删除需验证对应主密钥密码。")
            },
            confirmButton = {
                DeleteConfirmContent(
                    onConfirm = { pw ->
                        if (state.deleteSecret(secretId, pw)) {
                            showDeleteDialog = false
                            onBack()
                        }
                    },
                    onDismiss = { showDeleteDialog = false },
                )
            },
            dismissButton = {
                TextButton(onClick = { showDeleteDialog = false }) { Text("取消") }
            },
        )
    }

    // ---- 导出确认（需求 3.3：导出与数据文件逐字节相同） ----
    if (showExportConfirm) {
        AlertDialog(
            onDismissRequest = { showExportConfirm = false },
            title = { Text("导出机密信息") },
            text = { Text("导出内容与本地数据文件逐字节相同（已加密）。请保存到安全位置。") },
            confirmButton = {
                TextButton(onClick = {
                    showExportConfirm = false
                    state.requestSecretExport = secretId
                }) { Text("选择保存位置") }
            },
            dismissButton = {
                TextButton(onClick = { showExportConfirm = false }) { Text("取消") }
            },
        )
    }
}

/** 需求 3.5 的固定掩码（6 个星号），UI 不得自造。 */
internal const val ServiceMaskText = "******"

@Composable
private fun DetailLine(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
        Text(
            text = label,
            color = SkColors.TextSub,
            fontSize = 13.sp,
            modifier = Modifier.width(84.dp),
        )
        Text(text = value, color = SkColors.TextMain, fontSize = 13.sp)
    }
}

@Composable
private fun RevealPasswordDialog(
    keyName: String,
    needsPassword: Boolean,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var pw by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("验证主密钥密码") },
        text = {
            Column {
                Text("「$keyName」", fontSize = 13.sp, color = SkColors.TextSub)
                Spacer(Modifier.height(8.dp))
                if (needsPassword) {
                    SkPasswordField(pw, { pw = it }, "输入主密钥密码")
                } else {
                    Text("已解锁，直接查看。", fontSize = 13.sp)
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(pw) }, enabled = !needsPassword || pw.isNotEmpty()) {
                Text("查看")
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("取消") } },
    )
}

@Composable
private fun DeleteConfirmContent(
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var pw by remember { mutableStateOf("") }
    Column {
        SkPasswordField(pw, { pw = it }, "输入主密钥密码")
        Spacer(Modifier.height(10.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Button(
                onClick = { onConfirm(pw) },
                enabled = pw.isNotEmpty(),
                shape = RoundedCornerShape(8.dp),
                colors = ButtonDefaults.buttonColors(containerColor = SkColors.DangerText),
            ) { Text("删除") }
            OutlinedButton(onClick = onDismiss, shape = RoundedCornerShape(8.dp)) { Text("取消") }
        }
    }
}
