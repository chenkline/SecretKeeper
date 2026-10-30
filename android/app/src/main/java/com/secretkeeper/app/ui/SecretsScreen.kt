package com.secretkeeper.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.secretkeeper.app.core.SecretListItem

/**
 * 画板 14：机密信息列表。搜索框 + 行卡（标题/主密钥 ID/关联密钥名）。
 * 黄色问号：悬空引用（密钥缺失）显示「MK-XXXX?」——与名称为空无关（需求 3.5）。
 */
@Composable
fun SecretsScreen(
    state: AppViewModel,
    onOpenAddSecret: () -> Unit = {},
    onOpenImportSecret: () -> Unit = {},
    onOpenDetail: (String) -> Unit = {},
) {
    var query by remember { mutableStateOf("") }

    Column(modifier = Modifier.fillMaxSize().padding(20.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = "机密信息",
                color = SkColors.TextMain,
                fontSize = 22.sp,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.weight(1f),
            )
            androidx.compose.material3.Icon(
                SkIcons.Lock,
                contentDescription = null,
                tint = SkColors.Primary,
                modifier = Modifier.width(22.dp),
            )
        }
        Spacer(Modifier.height(14.dp))

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = state.quotaText,
                color = SkColors.TextSub,
                fontSize = 13.sp,
                modifier = Modifier.weight(1f),
            )
            OutlinedButton(
                onClick = onOpenImportSecret,
                shape = RoundedCornerShape(8.dp),
            ) { Text("📥 导入") }
            Spacer(Modifier.width(8.dp))
            Button(
                onClick = onOpenAddSecret,
                shape = RoundedCornerShape(8.dp),
                colors = ButtonDefaults.buttonColors(containerColor = SkColors.Primary),
            ) {
                androidx.compose.material3.Icon(
                    Icons.Filled.Add,
                    contentDescription = null,
                    modifier = Modifier.width(16.dp),
                )
                Text("新增")
            }
        }
        Spacer(Modifier.height(12.dp))

        OutlinedTextField(
            value = query,
            onValueChange = { query = it },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            placeholder = {
                Text("搜索标题或主密钥 ID", color = SkColors.TextFaint, fontSize = 14.sp)
            },
            colors = OutlinedTextFieldDefaults.colors(
                focusedBorderColor = SkColors.Primary,
                unfocusedBorderColor = SkColors.Border,
            ),
            shape = RoundedCornerShape(8.dp),
        )
        Spacer(Modifier.height(10.dp))

        // 表头（画板 14 的列头：标题（可选）/主密钥 ID/主密钥名称）
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(SkColors.PageBg)
                .padding(horizontal = 12.dp, vertical = 6.dp),
        ) {
            Text("标题（可选）／主密钥 ID／主密钥名称",
                color = SkColors.TextFaint, fontSize = 11.sp)
        }

        val filtered = state.secrets.filter { s ->
            query.isBlank() ||
                s.title.contains(query, ignoreCase = true) ||
                s.masterKeyId.contains(query, ignoreCase = true)
        }

        LazyColumn(modifier = Modifier.weight(1f)) {
            items(filtered, key = { it.secretId }) { item ->
                SecretRow(item, onClick = { onOpenDetail(item.secretId) })
            }
        }

        Spacer(Modifier.height(8.dp))
        Text(
            text = "黄色？表示 ID 表示密钥缺失；空名称不代表缺失。空标题与空名称保持留白。",
            color = SkColors.TextFaint,
            fontSize = 11.sp,
        )
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun SecretRow(item: SecretListItem, onClick: () -> Unit = {}) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() }
            .background(SkColors.CardBg, RoundedCornerShape(8.dp))
            .padding(horizontal = 12.dp, vertical = 10.dp),
    ) {

        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(modifier = Modifier.weight(1f)) {
                // 空标题与空名称保持留白（需求 3.1），不显示占位符
                Text(
                    text = item.title,
                    color = SkColors.TextMain,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.Medium,
                )
                Spacer(Modifier.height(4.dp))
                Row {
                    Text(
                        text = shortId(item.masterKeyId) + if (item.masterKeyFound) "" else "?",
                        color = if (item.masterKeyFound) SkColors.TextSub else SkColors.WarnText,
                        fontSize = 13.sp,
                    )
                    if (item.masterKeyName.isNotEmpty()) {
                        Spacer(Modifier.width(12.dp))
                        Text(text = item.masterKeyName, color = SkColors.TextSub, fontSize = 13.sp)
                    }
                }
            }
            Text(text = "›", color = SkColors.TextFaint, fontSize = 18.sp)
        }
    }
}
