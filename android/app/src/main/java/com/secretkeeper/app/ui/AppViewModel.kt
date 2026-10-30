package com.secretkeeper.app.ui

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.secretkeeper.app.core.BackoffStatus
import com.secretkeeper.app.core.FileStore
import com.secretkeeper.app.core.IndexDb
import com.secretkeeper.app.core.MasterKeyListItem
import com.secretkeeper.app.core.SecretListItem
import com.secretkeeper.app.core.Service
import com.secretkeeper.app.core.SvcMessages
import com.secretkeeper.app.core.SvcError
import java.io.File

/**
 * UI 状态与业务入口。UI 只通过 [service]（core 总入口）访问业务，
 * 不接触 crypto/serialize/store 的任何符号（AGENTS.md 四层约束）。
 */
class AppViewModel(dataDir: File) {
    val service: Service

    /** 是否已解锁（决定 Unlock/MainScaffold 路由）。 */
    var unlocked by mutableStateOf(false)
        private set

    var masterKeys by mutableStateOf<List<MasterKeyListItem>>(emptyList())
        private set

    var secrets by mutableStateOf<List<SecretListItem>>(emptyList())
        private set

    var quotaText by mutableStateOf("")
        private set

    /** 解锁页错误文案（需求文档固定文案，见 SvcMessages）。 */
    var unlockError by mutableStateOf<String?>(null)
        private set

    /** 解锁页退避状态。 */
    var backoff by mutableStateOf(BackoffStatus())
        private set

    /** 默认主密钥名称（解锁页选择器展示用）。 */
    var defaultKeyName by mutableStateOf("")
        private set

    var defaultKeyId by mutableStateOf("")
        private set

    init {
        val db = IndexDb(AndroidSqlDb.open(File(dataDir, "secret.db").absolutePath))
        db.createSchema()
        service = Service(db, FileStore(dataDir))
        service.open()
        refreshLists()
    }

    /** 刷新列表与配额展示。解锁/锁定/增删后都应调用。 */
    fun refreshLists() {
        masterKeys = service.listMasterKeys()
        secrets = service.listSecrets()
        val q = service.quota()
        quotaText = "${q.secrets} / ${q.maxSecrets} 条 · 基础版"
        val defId = service.defaultMasterKeyId()
        defaultKeyId = defId ?: ""
        defaultKeyName = masterKeys.firstOrNull { it.masterKeyId == defId }?.name ?: ""
    }

    /** 解锁。失败文案取需求固定文案；退避剩余秒数刷新到状态。 */
    fun unlock(masterKeyId: String, password: CharArray) {
        val err = service.unlock(masterKeyId, password.toStringByteArray())
        backoff = service.backoffStatus()
        if (err == SvcError.OK) {
            unlockError = null
            unlocked = true
            refreshLists()
        } else {
            // 对外文案按需求固定，不泄露内部细节（AGENTS.md §8）
            val base = com.secretkeeper.app.core.SvcMessages.message(err)
            unlockError = if (backoff.waiting && backoff.remainingSeconds > 0) {
                "$base，请 ${backoff.remainingSeconds} 秒后重试"
            } else {
                base
            }
        }
    }

    /** 锁定（解锁页「已锁定」徽章对应的反向操作；闲置计时由 UI 层驱动）。 */
    fun lock() {
        service.lock()
        unlocked = false
        refreshLists()
    }

    /** 表单页错误文案（需求固定文案）。 */
    var formError by mutableStateOf<String?>(null)
        private set

    /** 生成主密钥。成功返回 true（UI 返回列表页并刷新）。 */
    fun createMasterKey(name: String, pw: CharArray): Boolean {
        val err = service.createMasterKey(name, pw.toStringByteArray())
        formError = if (err == SvcError.OK) null else SvcMessages.message(err)
        if (err == SvcError.OK) refreshLists()
        return err == SvcError.OK
    }

    /** 新增机密信息。成功返回 true。 */
    fun addSecret(title: String, content: String, mkId: String, pw: CharArray): Boolean {
        val err = service.addSecret(title, content, mkId, pw.toStringByteArray())
        formError = if (err == SvcError.OK) null else SvcMessages.message(err)
        if (err == SvcError.OK) refreshLists()
        return err == SvcError.OK
    }

    fun notifyActivity() {
        service.notifyActivity()
    }

    // ------------------------------------------------ 详情页（画板 15/16）

    var detail by mutableStateOf<com.secretkeeper.app.core.SecretDetail?>(null)
        private set

    var detailError by mutableStateOf<String?>(null)
        private set

    private var detailPassword by mutableStateOf("")

    /**
     * 加载详情。masterPassword 为空时返回掩码态（不报错，需求 3.5：
     * UI 先展示再决定是否索要密码）。
     */
    fun loadDetail(secretId: String, masterPassword: String = "") {
        detailPassword = masterPassword
        val (e, d) = service.getSecretDetail(secretId, masterPassword.toByteArray())
        detailError = if (e == SvcError.OK) null else SvcMessages.message(e)
        detail = d
    }

    /** 「查看」：非默认密钥带密码重新取明文；失败文案写 detailError。 */
    fun revealDetail(secretId: String, password: String) {
        detailPassword = password
        val (e, plain) = service.revealSecretPlaintext(secretId, password.toByteArray())
        if (e == SvcError.OK) {
            detailError = null
            detail = detail?.copy(plaintextMasked = false, plaintext = plain ?: "")
        } else {
            detailError = SvcMessages.message(e)
        }
    }

    /** 切换界面 / 进入后台立即隐藏明文（需求 3.5 的固定行为）。 */
    fun maskDetail() {
        detail = detail?.copy(plaintextMasked = true, plaintext = "")
    }

    /** 删除机密信息。主密钥缺失时跳过密码校验（与桌面一致）。 */
    fun deleteSecret(secretId: String, password: String): Boolean {
        val e = service.deleteSecret(secretId, password.toByteArray())
        if (e == SvcError.OK) {
            refreshLists()
            detail = null
            return true
        }
        detailError = SvcMessages.message(e)
        return false
    }

    // ------------------------------------------ 主密钥操作（需求 2.5/2.6）

    /**
     * 切换默认主密钥（需求 2.5：原默认与新默认两把密钥的密码都必须验证；
     * 已解锁时缓存 KEK 可作会话凭证，两个密码都可留空）。
     */
    fun switchDefault(targetId: String, currentPw: String, newPw: String): Boolean {
        val e = service.switchDefaultMasterKey(targetId,
            currentPw.toByteArray(), newPw.toByteArray())
        formError = if (e == SvcError.OK) null else SvcMessages.message(e)
        if (e == SvcError.OK) {
            // 切换后缓存已清（service 内部语义），回解锁页重新解锁
            unlocked = false
            refreshLists()
        }
        return e == SvcError.OK
    }

    /** 删除非默认主密钥（默认禁止删除由 service 判定；需该密钥密码）。 */
    fun deleteMasterKey(masterKeyId: String, password: String): Boolean {
        val e = service.deleteMasterKey(masterKeyId, password.toByteArray())
        formError = if (e == SvcError.OK) null else SvcMessages.message(e)
        if (e == SvcError.OK) refreshLists()
        return e == SvcError.OK
    }

    // ------------------------------------------------ 导入导出（画板 13/18/19）

    /** 详情页导出请求：MainActivity 观察后触发 SAF 保存对话框。 */
    var requestSecretExport by mutableStateOf<String?>(null)

    /** 主密钥导出的保护密码（对话框临时输入，用后清空）。 */
    var exportProtectionPw by mutableStateOf("")
    var pendingExportKey by mutableStateOf<String?>(null)

    fun isDefaultKey(masterKeyId: String): Boolean =
        service.defaultMasterKeyId() == masterKeyId

    /** 导出主密钥字节流（保护密码新设，不能与主密钥密码混用）。 */
    fun exportMasterKey(masterKeyId: String, protectionPassword: String): Pair<SvcError, ByteArray?> =
        service.exportMasterKey(masterKeyId, detailPassword.toByteArray(),
            protectionPassword.toByteArray())

    /** 导出机密信息：与数据文件逐字节相同，不需要密码（需求 3.3）。 */
    fun exportSecret(secretId: String): Pair<SvcError, ByteArray?> =
        service.exportSecret(secretId, ByteArray(0))

    /** 导入主密钥：protectionPassword 解文件，newPassword 重加密保存。 */
    fun importMasterKey(bytes: ByteArray, protectionPassword: String, newPassword: String): Boolean {
        val e = service.importMasterKey(bytes, protectionPassword.toByteArray(),
            newPassword.toByteArray())
        formError = if (e == SvcError.OK) null else SvcMessages.message(e)
        if (e == SvcError.OK) refreshLists()
        return e == SvcError.OK
    }

    /** 导入机密信息：校验能解开才入库。 */
    fun importSecret(bytes: ByteArray, masterPassword: String): Boolean {
        val e = service.importSecret(bytes, ByteArray(0), masterPassword.toByteArray())
        formError = if (e == SvcError.OK) null else SvcMessages.message(e)
        if (e == SvcError.OK) refreshLists()
        return e == SvcError.OK
    }

    /** 需求：锁定态下每 30 秒轮换一次 KEK（需求 2.7）。 */
    fun rotateKekTick(): Boolean =
        service.rotateKek() == SvcError.OK
}

/** CharArray -> ByteArray（密码以字节传给 service；调用方负责事后清零）。 */
private fun CharArray.toStringByteArray(): ByteArray {
    val out = ByteArray(size)
    for (i in indices) out[i] = this[i].code.toByte()
    return out
}
