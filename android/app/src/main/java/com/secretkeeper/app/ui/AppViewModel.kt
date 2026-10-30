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

    fun notifyActivity() {
        service.notifyActivity()
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
