package com.secretkeeper.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.ui.platform.LocalContext

/**
 * 画板 13/18：导入主密钥与导入机密信息表单。
 * 文件选择走 SAF（OpenDocument），不申请任何存储权限；导出走 CreateDocument。
 */
@Composable
fun ImportKeyForm(state: AppViewModel, onBack: () -> Unit) {
    var fileName by remember { mutableStateOf("") }
    var bytes by remember { mutableStateOf<ByteArray?>(null) }
    var protPw by remember { mutableStateOf("") }
    var newPw by remember { mutableStateOf("") }
    val context = LocalContext.current

    val picker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri != null) {
            fileName = uri.lastPathSegment ?: "已选择文件"
            bytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
        }
    }

    FormPageShell(title = "导入主密钥", subtitle = "使用导出文件的保护密码，导入为主密钥。") {
        // 文件选择卡（画板 13）
        Column(
            modifier = Modifier.fillMaxWidth().background(SkColors.CardBg, SkCardShape)
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            androidx.compose.material3.Icon(
                SkIcons.Key, contentDescription = null,
                tint = SkColors.Primary, modifier = Modifier.width(34.dp),
            )
            Spacer(Modifier.height(8.dp))
            Text(
                text = fileName.ifEmpty { "未选择文件" },
                color = if (fileName.isEmpty()) SkColors.TextFaint else SkColors.TextMain,
                fontSize = 15.sp,
                fontWeight = FontWeight.SemiBold,
            )
            Spacer(Modifier.height(4.dp))
            Text(
                text = if (bytes != null) "本地文件 · 已选择" else "本地文件",
                color = SkColors.TextSub,
                fontSize = 12.sp,
            )
            Spacer(Modifier.height(10.dp))
            OutlinedButton(
                onClick = { picker.launch(arrayOf("*/*")) },
                shape = RoundedCornerShape(8.dp),
            ) { Text(if (bytes == null) "选择文件" else "重新选择文件") }
        }
        Spacer(Modifier.height(16.dp))

        FormLabel("文件保护密码")
        Spacer(Modifier.height(6.dp))
        SkPasswordField(protPw, { protPw = it }, "输入导出时设置的保护密码")
        Spacer(Modifier.height(12.dp))

        FormLabel("新的主密钥密码")
        Spacer(Modifier.height(6.dp))
        SkPasswordField(newPw, { newPw = it }, "为本机保存的主密钥新设密码")
        Spacer(Modifier.height(6.dp))
        Text(text = "为本机保存的主密钥新设密码。", color = SkColors.TextSub, fontSize = 12.sp)
        Spacer(Modifier.height(12.dp))

        // 限额提示（业务规则在 service；导入新增一把主密钥）
        if (state.masterKeys.size >= 2) {
            Column(
                modifier = Modifier.fillMaxWidth()
                    .background(SkColors.WarnBg, RoundedCornerShape(8.dp))
                    .padding(12.dp),
            ) {
                Text(text = "⚠ 容量已满 · ${state.masterKeys.size}/2",
                    color = SkColors.WarnText, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(4.dp))
                Text(text = "先导出并删除一把主密钥，再导入此文件。",
                    color = SkColors.WarnText, fontSize = 12.sp)
            }
            Spacer(Modifier.height(12.dp))
        }

        state.formError?.let { msg ->
            Text(text = msg, color = SkColors.DangerText, fontSize = 13.sp)
            Spacer(Modifier.height(8.dp))
        }

        Button(
            onClick = {
                if (bytes != null && state.importMasterKey(bytes!!, protPw, newPw)) onBack()
            },
            enabled = bytes != null && protPw.isNotEmpty() && newPw.isNotEmpty(),
            modifier = Modifier.fillMaxWidth().height(46.dp),
            shape = RoundedCornerShape(8.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = SkColors.Primary,
                disabledContainerColor = SkColors.PillBg,
                disabledContentColor = SkColors.TextSub,
            ),
        ) { Text("导入主密钥") }
        Spacer(Modifier.height(8.dp))
        Text(
            text = "主密钥密码丢失无法解密。名称来自文件，可保留为空；第一把主密钥自动设为默认。",
            color = SkColors.TextFaint,
            fontSize = 11.sp,
        )
    }
}

@Composable
fun ImportSecretForm(state: AppViewModel, onBack: () -> Unit) {
    var fileName by remember { mutableStateOf("") }
    var bytes by remember { mutableStateOf<ByteArray?>(null) }
    var mkPw by remember { mutableStateOf("") }
    val context = LocalContext.current

    val picker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri != null) {
            fileName = uri.lastPathSegment ?: "已选择文件"
            bytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
        }
    }

    FormPageShell(title = "导入机密信息", subtitle = "导入导出的 .ssc 文件；数据密钥由主密钥封装。") {
        Column(
            modifier = Modifier.fillMaxWidth().background(SkColors.CardBg, SkCardShape)
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            androidx.compose.material3.Icon(
                SkIcons.Lock, contentDescription = null,
                tint = SkColors.Primary, modifier = Modifier.width(34.dp),
            )
            Spacer(Modifier.height(8.dp))
            Text(
                text = fileName.ifEmpty { "未选择文件" },
                color = if (fileName.isEmpty()) SkColors.TextFaint else SkColors.TextMain,
                fontSize = 15.sp,
                fontWeight = FontWeight.SemiBold,
            )
            Spacer(Modifier.height(10.dp))
            OutlinedButton(
                onClick = { picker.launch(arrayOf("*/*")) },
                shape = RoundedCornerShape(8.dp),
            ) { Text(if (bytes == null) "选择文件" else "重新选择文件") }
        }
        Spacer(Modifier.height(16.dp))

        FormLabel("主密钥密码")
        Spacer(Modifier.height(6.dp))
        SkPasswordField(mkPw, { mkPw = it }, "输入对应主密钥的密码")
        Spacer(Modifier.height(6.dp))
        Text(
            text = "导入前会先验证能解开数据；ID 冲突时自动换新 ID，不覆盖本地记录。",
            color = SkColors.TextSub,
            fontSize = 12.sp,
        )
        Spacer(Modifier.height(12.dp))

        state.formError?.let { msg ->
            Text(text = msg, color = SkColors.DangerText, fontSize = 13.sp)
            Spacer(Modifier.height(8.dp))
        }

        Button(
            onClick = {
                if (bytes != null && state.importSecret(bytes!!, mkPw)) onBack()
            },
            enabled = bytes != null && mkPw.isNotEmpty(),
            modifier = Modifier.fillMaxWidth().height(46.dp),
            shape = RoundedCornerShape(8.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = SkColors.Primary,
                disabledContainerColor = SkColors.PillBg,
                disabledContentColor = SkColors.TextSub,
            ),
        ) { Text("导入机密信息") }
    }
}

/** 表单页公共骨架（返回条已在 FormScaffold，这里只做滚动容器与标题区）。 */
@Composable
private fun FormPageShell(
    title: String,
    subtitle: String,
    content: @Composable () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(20.dp),
    ) {
        Text(text = title, color = SkColors.TextMain, fontSize = 22.sp,
            fontWeight = FontWeight.SemiBold)
        Spacer(Modifier.height(6.dp))
        Text(text = subtitle, color = SkColors.TextSub, fontSize = 13.sp)
        Spacer(Modifier.height(16.dp))
        content()
    }
}
