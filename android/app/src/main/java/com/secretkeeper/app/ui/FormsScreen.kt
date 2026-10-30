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
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
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
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.foundation.text.KeyboardOptions
import com.secretkeeper.app.core.MasterKeyListItem
import com.secretkeeper.app.core.Service
import com.secretkeeper.app.core.ServiceLimits

/**
 * 画板 12/17：生成主密钥与新增机密信息表单。表单页从列表页以覆盖路由打开。
 */
enum class SkForm { CREATE_KEY, ADD_SECRET, IMPORT_KEY, IMPORT_SECRET }

@Composable
fun FormScaffold(
    state: AppViewModel,
    form: SkForm,
    onBack: () -> Unit,
) {
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
                text = when (form) {
                    SkForm.CREATE_KEY -> "生成主密钥"
                    SkForm.ADD_SECRET -> "新增机密信息"
                    SkForm.IMPORT_KEY -> "导入主密钥"
                    SkForm.IMPORT_SECRET -> "导入机密信息"
                },
                color = SkColors.TextMain,
                fontSize = 17.sp,
                fontWeight = FontWeight.SemiBold,
            )
        }
        when (form) {
            SkForm.CREATE_KEY -> CreateKeyForm(state, onBack)
            SkForm.ADD_SECRET -> AddSecretForm(state, onBack)
            SkForm.IMPORT_KEY -> ImportKeyForm(state, onBack)
            SkForm.IMPORT_SECRET -> ImportSecretForm(state, onBack)
        }
    }
}

// ------------------------------------------------------------- 生成主密钥

@Composable
private fun CreateKeyForm(state: AppViewModel, onBack: () -> Unit) {
    var name by remember { mutableStateOf("") }
    var pw by remember { mutableStateOf("") }
    var confirm by remember { mutableStateOf("") }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(20.dp),
    ) {
        Text(
            text = "生成主密钥",
            color = SkColors.TextMain,
            fontSize = 22.sp,
            fontWeight = FontWeight.SemiBold,
        )
        Spacer(Modifier.height(6.dp))
        Text(text = "在本机生成一把随机 RSA-2048 主密钥。", color = SkColors.TextSub, fontSize = 13.sp)
        Spacer(Modifier.height(20.dp))

        FormLabel("主密钥名称（可选）")
        Spacer(Modifier.height(6.dp))
        OutlinedTextField(
            value = name,
            onValueChange = { name = it },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            placeholder = { Text("可以留空", color = SkColors.TextFaint, fontSize = 14.sp) },
            supportingText = { Text("名称仅用于辨识，不参与加密。", color = SkColors.TextSub, fontSize = 11.sp) },
            colors = OutlinedTextFieldDefaults.colors(
                focusedBorderColor = SkColors.Primary,
                unfocusedBorderColor = SkColors.Border,
            ),
            shape = RoundedCornerShape(8.dp),
        )
        Spacer(Modifier.height(12.dp))

        FormLabel("主密钥密码")
        Spacer(Modifier.height(6.dp))
        SkPasswordField(pw, { pw = it }, "输入主密钥密码")
        Spacer(Modifier.height(12.dp))

        FormLabel("确认主密钥密码")
        Spacer(Modifier.height(6.dp))
        SkPasswordField(confirm, { confirm = it }, "再次输入主密钥密码")

        // 两次输入不一致的行内提示（画板 12 的行内错误约定）
        if (confirm.isNotEmpty() && pw != confirm) {
            Spacer(Modifier.height(8.dp))
            Text(text = "两次输入的密码不一致", color = SkColors.DangerText, fontSize = 12.sp)
        }
        Spacer(Modifier.height(14.dp))

        // 警示条（需求固定文案：密码不可找回）
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(SkColors.WarnBg, RoundedCornerShape(8.dp))
                .padding(12.dp),
        ) {
            Text(text = "请妥善记住主密钥密码", color = SkColors.WarnText, fontSize = 13.sp,
                fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(4.dp))
            Text(
                text = "密码丢失无法解密，密匣无法为你重置密码。",
                color = SkColors.WarnText,
                fontSize = 12.sp,
            )
        }

        state.formError?.let { msg ->
            Spacer(Modifier.height(10.dp))
            Text(text = msg, color = SkColors.DangerText, fontSize = 13.sp)
        }

        Spacer(Modifier.height(18.dp))
        val ok = pw.isNotEmpty() && pw == confirm
        Button(
            onClick = {
                if (state.createMasterKey(name.trim(), pw.toCharArray())) onBack()
            },
            enabled = ok,
            shape = RoundedCornerShape(8.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = SkColors.Primary,
                disabledContainerColor = SkColors.PillBg,
                disabledContentColor = SkColors.TextSub,
            ),
        ) { Text("生成主密钥") }
    }
}

// ------------------------------------------------------------ 新增机密信息

@Composable
private fun AddSecretForm(state: AppViewModel, onBack: () -> Unit) {
    var title by remember { mutableStateOf("") }
    var content by remember { mutableStateOf("") }
    var selectedId by remember { mutableStateOf(state.defaultKeyId) }
    var mkPassword by remember { mutableStateOf("") }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(20.dp),
    ) {
        Row {
            Text(
                text = "自由文本，本地加密保存。",
                color = SkColors.TextSub,
                fontSize = 13.sp,
                modifier = Modifier.weight(1f),
            )
            Text(
                text = "${Service.secretCharCount(content)} / ${ServiceLimits.MAX_SECRET_CHARS}",
                color = SkColors.TextSub,
                fontSize = 13.sp,
            )
        }
        Spacer(Modifier.height(14.dp))

        FormLabel("用于加密的主密钥")
        Spacer(Modifier.height(6.dp))

        // 密钥选择列表（画板 17：勾选行 + 空名保持留白 + ID 辨识）
        state.masterKeys.forEach { item ->
            KeyPickRow(item, selected = item.masterKeyId == selectedId) {
                selectedId = item.masterKeyId
            }
            Spacer(Modifier.height(6.dp))
        }
        Text(
            text = "空名称保持留白，可通过 ID 辨识；选该密钥需输密码。",
            color = SkColors.TextFaint,
            fontSize = 11.sp,
        )
        Spacer(Modifier.height(14.dp))

        FormLabel("标题（可选）")
        Spacer(Modifier.height(6.dp))
        OutlinedTextField(
            value = title,
            onValueChange = { title = it },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            placeholder = { Text("不填写则保持为空", color = SkColors.TextFaint, fontSize = 14.sp) },
            colors = OutlinedTextFieldDefaults.colors(
                focusedBorderColor = SkColors.Primary,
                unfocusedBorderColor = SkColors.Border,
            ),
            shape = RoundedCornerShape(8.dp),
        )
        Spacer(Modifier.height(12.dp))

        FormLabel("机密信息")
        Spacer(Modifier.height(6.dp))
        val overLimit = Service.secretCharCount(content) > ServiceLimits.MAX_SECRET_CHARS
        OutlinedTextField(
            value = content,
            onValueChange = { content = it },
            modifier = Modifier.fillMaxWidth().height(140.dp),
            placeholder = { Text("输入机密内容", color = SkColors.TextFaint, fontSize = 14.sp) },
            isError = overLimit,
            colors = OutlinedTextFieldDefaults.colors(
                focusedBorderColor = SkColors.Primary,
                unfocusedBorderColor = SkColors.Border,
                errorBorderColor = SkColors.DangerText,
            ),
            shape = RoundedCornerShape(8.dp),
        )
        Row {
            Text(
                text = "最多 150 字符",
                color = SkColors.TextFaint,
                fontSize = 11.sp,
                modifier = Modifier.weight(1f),
            )
            if (overLimit) {
                Text(text = "超出 150 字符", color = SkColors.DangerText, fontSize = 11.sp)
            }
        }
        Spacer(Modifier.height(10.dp))

        // 非默认密钥需要该密钥的密码（kNeedsPassword 语义的表单化）
        val needsPassword = selectedId != state.defaultKeyId
        if (needsPassword) {
            FormLabel("「${keyNameOf(state, selectedId)}」的密码")
            Spacer(Modifier.height(6.dp))
            SkPasswordField(mkPassword, { mkPassword = it }, "输入该主密钥的密码")
            Spacer(Modifier.height(6.dp))
        }

        state.formError?.let { msg ->
            Text(text = msg, color = SkColors.DangerText, fontSize = 13.sp)
            Spacer(Modifier.height(8.dp))
        }

        Spacer(Modifier.height(8.dp))
        Button(
            onClick = {
                if (state.addSecret(title.trim(), content, selectedId, mkPassword.toCharArray())) {
                    onBack()
                }
            },
            enabled = content.isNotEmpty() && !overLimit,
            modifier = Modifier.fillMaxWidth().height(46.dp),
            shape = RoundedCornerShape(8.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = SkColors.Primary,
                disabledContainerColor = SkColors.PillBg,
                disabledContentColor = SkColors.TextSub,
            ),
        ) { Text("🔒 保存") }
        Spacer(Modifier.height(8.dp))
        Text(
            text = "AES-256-GCM 加密内容；RSA-OAEP-SHA256 保护数据密钥。",
            color = SkColors.TextFaint,
            fontSize = 11.sp,
        )
    }
}

@Composable
private fun KeyPickRow(item: MasterKeyListItem, selected: Boolean, onPick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(
                if (selected) SkColors.PrimaryWash else SkColors.CardBg,
                RoundedCornerShape(8.dp),
            )
            .padding(12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        OutlinedButton(onClick = onPick) {
            Text(if (selected) "✓" else "　", color = SkColors.Primary, fontSize = 13.sp)
        }
        Text(
            text = item.name, // 空名称保持留白（需求 3.1）
            color = SkColors.TextMain,
            fontSize = 14.sp,
            fontWeight = FontWeight.Medium,
            modifier = Modifier.weight(1f),
        )
        Text(text = shortId(item.masterKeyId), color = SkColors.TextSub, fontSize = 13.sp)
    }
}

private fun keyNameOf(state: AppViewModel, id: String): String =
    state.masterKeys.firstOrNull { it.masterKeyId == id }?.name ?: id

// ------------------------------------------------------------------ 公共件

@Composable
internal fun FormLabel(text: String) {
    Text(text = text, color = SkColors.TextMain, fontSize = 13.sp)
}

@Composable
internal fun SkPasswordField(
    value: String,
    onValueChange: (String) -> Unit,
    placeholder: String,
) {
    var visible by remember { mutableStateOf(false) }
    OutlinedTextField(
        value = value,
        onValueChange = onValueChange,
        modifier = Modifier.fillMaxWidth(),
        singleLine = true,
        placeholder = { Text(placeholder, color = SkColors.TextFaint, fontSize = 14.sp) },
        visualTransformation = if (visible) VisualTransformation.None else PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(
            keyboardType = KeyboardType.Password,
            capitalization = KeyboardCapitalization.None,
        ),
        trailingIcon = {
            Text(text = if (visible) "🙈" else "👁", fontSize = 14.sp,
                modifier = Modifier.padding(end = 8.dp))
        },
        colors = OutlinedTextFieldDefaults.colors(
            focusedBorderColor = SkColors.Primary,
            unfocusedBorderColor = SkColors.Border,
        ),
        shape = RoundedCornerShape(8.dp),
    )
}
