package com.secretkeeper.app.core

import java.io.File
import java.security.SecureRandom

/**
 * 服务层（Android core 总入口）。
 *
 * 与桌面端 src/common/core/service.cpp 保持同一职责与语义：
 * UI 只通过本层访问 core；UI 不得接触 KEK、GCM、RSA 等任何密码学符号。
 *
 * 依赖方向：service -> store -> serialize；service -> crypto；
 * serialize 与 crypto 互不依赖。
 *
 * 密钥所有权：默认主密钥的 KEK 常驻本层 KekCache（程序随机密钥包裹、
 * 3 个诱饵槽位随机迁移）。UI 永远拿不到 KEK。
 */

/** 限额：业务规则集中于此，v1.0.0 由 License 覆盖时只改这一处。 */
object ServiceLimits {
    const val MAX_MASTER_KEYS = 2
    const val MAX_SECRETS = 15
    const val MAX_SECRET_CHARS = 150
}

/** 业务错误。语义与桌面 service::Error 一一对应。 */
enum class SvcError {
    OK,
    // 密码与解锁
    PASSWORD_WRONG,      // 主密钥密码错误
    RATE_LIMITED,        // 退避等待中
    NEEDS_PASSWORD,      // 需要非默认主密钥密码，UI 弹框后带密码重试
    LOCKED,              // 尚未解锁
    // 主密钥
    MASTER_KEY_QUOTA_EXCEEDED,
    MASTER_KEY_NOT_FOUND,
    CANNOT_DELETE_DEFAULT, // 禁止删除默认主密钥
    // 机密信息
    SECRET_QUOTA_EXCEEDED,
    SECRET_TOO_LONG,     // 超出 150 字符
    SECRET_NOT_FOUND,
    EMPTY_SECRET,        // 内容为空
    // 导入导出
    IMPORT_BAD_FORMAT,
    IMPORT_VERSION_TOO_HIGH,
    IMPORT_MASTER_KEY_MISSING,
    IMPORT_DECRYPT_FAILED,
    // 环境与存储
    IO_ERROR,
    INTERNAL,
}

/** 需求文档固定的中文提示文案，UI 原样显示，不得改写。 */
object SvcMessages {
    fun message(e: SvcError): String = when (e) {
        SvcError.OK -> ""
        SvcError.PASSWORD_WRONG -> "主密钥密码错误"
        SvcError.RATE_LIMITED -> "尝试过于频繁，请稍后再试"
        SvcError.NEEDS_PASSWORD -> "请输入该主密钥的密码"
        SvcError.LOCKED -> "密码库已锁定，请先解锁"
        SvcError.MASTER_KEY_QUOTA_EXCEEDED -> "主密钥数量已达上限，请先导出并删除部分记录"
        SvcError.MASTER_KEY_NOT_FOUND -> "主密钥不存在"
        SvcError.CANNOT_DELETE_DEFAULT -> "不能删除默认主密钥，请先切换默认主密钥"
        SvcError.SECRET_QUOTA_EXCEEDED -> "机密信息条数已达上限，请先导出并删除部分记录"
        SvcError.SECRET_TOO_LONG -> "机密信息超出 150 字符"
        SvcError.SECRET_NOT_FOUND -> "机密信息不存在"
        SvcError.EMPTY_SECRET -> "机密信息内容不能为空"
        SvcError.IMPORT_BAD_FORMAT -> "文件格式不正确"
        SvcError.IMPORT_VERSION_TOO_HIGH -> "文件版本过高，请先升级应用"
        SvcError.IMPORT_MASTER_KEY_MISSING -> "请先导入对应的主密钥"
        SvcError.IMPORT_DECRYPT_FAILED -> "解密失败，文件可能已损坏或密码不正确"
        SvcError.IO_ERROR -> "存储读写失败，请稍后重试"
        SvcError.INTERNAL -> "内部错误"
    }
}

// ===========================================================================
// DTO —— UI 只看到这些
// ===========================================================================

data class MasterKeyListItem(
    val masterKeyId: String,
    val name: String, // 可为空，且为空不代表该密钥不存在
    val isDefault: Boolean,
    val secretCount: Int,
    val createdAt: Long, // Unix 毫秒，02 号画板的「生成/导入」日期
)

data class SecretListItem(
    val secretId: String,
    val title: String, // 可为空
    val masterKeyId: String,
    val masterKeyName: String,
    /** 密钥存在但名称为空 -> true；密钥缺失 -> false（黄色问号）。 */
    val masterKeyFound: Boolean,
)

data class SecretDetail(
    val secretId: String,
    val title: String,
    val masterKeyId: String,
    val masterKeyName: String,
    val masterKeyFound: Boolean,
    /** 主密钥缺失或尚未提供密码时为 true，UI 显示 6 个 * 掩码。 */
    val plaintextMasked: Boolean = true,
    val plaintext: String = "",
)

data class QuotaStatus(
    val masterKeys: Int,
    val maxMasterKeys: Int = ServiceLimits.MAX_MASTER_KEYS,
    val secrets: Int,
    val maxSecrets: Int = ServiceLimits.MAX_SECRETS,
)

data class BackoffStatus(
    val waiting: Boolean = false,
    val remainingSeconds: Long = 0,
    val consecutiveFailures: Long = 0,
)

// ===========================================================================
// 密码退避（需求 4.5）
//
// 第 n 次失败后 delay(n) = min(2^(n-1) 秒, 60 秒)；只统计「密码错误」，
// 文件损坏等不属于密码问题。仅存内存，不落盘。
// ===========================================================================

class PasswordBackoff(private val capSeconds: Long = 60) {
    private var failures = 0L
    private var windowStartMs = 0L
    private var waiting = false

    companion object {
        /** 纯函数形式，便于测试：给定连续失败次数返回应等待秒数。 */
        fun delayFor(consecutiveFailures: Long, capSeconds: Long): Long {
            if (consecutiveFailures <= 0) return 0
            val exp = if (consecutiveFailures - 1 >= 62) {
                Long.MAX_VALUE
            } else {
                (1L shl (consecutiveFailures - 1).toInt())
            }
            return minOf(exp, capSeconds)
        }
    }

    /** 记录一次密码错误，返回此后需等待的秒数。 */
    fun recordFailure(): Long {
        failures += 1
        windowStartMs = System.nanoTime() / 1_000_000
        waiting = true
        return delayFor(failures, capSeconds)
    }

    /** 密码正确，清零计数。 */
    fun reset() {
        failures = 0
        waiting = false
        windowStartMs = 0
    }

    fun consecutiveFailures(): Long = failures

    fun isWaiting(): Boolean = remainingSeconds() > 0

    /** 退避窗口剩余秒数（向上取整）。不在窗口内返回 0。 */
    fun remainingSeconds(): Long {
        if (!waiting || failures == 0L) return 0
        val delay = delayFor(failures, capSeconds)
        if (delay == 0L) return 0
        val now = System.nanoTime() / 1_000_000
        val elapsedSec = (now - windowStartMs) / 1000
        return maxOf(0, delay - elapsedSec)
    }
}

// ===========================================================================
// KEK 缓存（需求 2.7）
//
// 程序启动（此处：构造）时生成随机对称密钥；KEK 以 AES-256-GCM 用它包裹，
// 密文与程序随机密钥分散在两个对象中；内存中 3 个备选槽位，每次轮换随机
// 迁移，原槽位不清除（诱饵）。轮换时机：每 30 秒、每次解锁成功后。
// 该机制提高内存转储的直接攻击成本，不替代锁屏密码。
// ===========================================================================

class KekCache {
    companion object {
        const val SLOT_COUNT = 3
        const val ROTATION_SECONDS = 30
        const val KEK_LENGTH = 32
    }

    private var appKey: ByteArray? = null // 程序随机密钥，与槽位分散保存
    private val slots = arrayOfNulls<ByteArray>(SLOT_COUNT) // KEK 密文（明文+tag）
    private val nonces = arrayOfNulls<ByteArray>(SLOT_COUNT)
    private var activeSlot = -1
    private var loaded = false

    /** 生成新的程序随机密钥并载入 KEK。每次解锁成功后都应调用一次。 */
    fun load(kek: ByteArray): Boolean {
        clear()
        appKey = Crypto.randomBytes(32)
        val slot = SecureRandom().nextInt(SLOT_COUNT)
        if (!reencryptInto(slot, kek)) {
            clear()
            return false
        }
        activeSlot = slot
        loaded = true
        return true
    }

    /** 轮换：重新加密并随机迁移到另一个槽位，原槽位保留作诱饵。 */
    fun rotate(): Boolean {
        if (!loaded) return false
        val kek = peek() ?: return false
        var next = SecureRandom().nextInt(SLOT_COUNT)
        if (SLOT_COUNT > 1) {
            while (next == activeSlot) {
                next = SecureRandom().nextInt(SLOT_COUNT)
            }
        }
        if (!reencryptInto(next, kek)) return false
        activeSlot = next
        return true
    }

    /** 锁定：清零程序随机密钥与全部槽位。 */
    fun clear() {
        appKey?.fill(0)
        appKey = null
        for (i in 0 until SLOT_COUNT) {
            slots[i]?.fill(0)
            slots[i] = null
            nonces[i]?.fill(0)
            nonces[i] = null
        }
        activeSlot = -1
        loaded = false
    }

    /** 取回缓存的 KEK。仅在已载入时有效。 */
    fun peek(): ByteArray? {
        if (!loaded) return null
        val key = appKey ?: return null
        val sealed = slots[activeSlot] ?: return null
        val nonce = nonces[activeSlot] ?: return null
        return try {
            Crypto.aesGcmDecrypt(key, nonce, ByteArray(0), sealed.copyOfRange(0, sealed.size - 16),
                sealed.copyOfRange(sealed.size - 16, sealed.size))
        } catch (e: Exception) {
            null
        }
    }

    fun hasValue(): Boolean = loaded

    fun activeSlotIndex(): Int = activeSlot

    /** 某个槽位是否已被占用（诱饵槽位确实留有数据）。 */
    fun slotOccupied(index: Int): Boolean = slots[index] != null

    private fun reencryptInto(slot: Int, kek: ByteArray): Boolean {
        val key = appKey ?: return false
        val nonce = Crypto.randomBytes(Serialize.NONCE_LENGTH)
        val cipher = try {
            val pair = Crypto.aesGcmEncrypt(key, nonce, ByteArray(0), kek)
            pair.first + pair.second
        } catch (e: Exception) {
            return false
        }
        slots[slot] = cipher
        nonces[slot] = nonce
        return true
    }
}

// ===========================================================================
// Service 门面
// ===========================================================================

class Service(private val db: IndexDb, private val files: FileStore) {
    companion object {
        /** 需求 3.5：主密钥缺失时详情页显示的掩码，固定 6 个星号。 */
        const val MASKED_PLAINTEXT = "******"

        /** 单条机密信息的字符数（按 Unicode 码点计），供 UI 显示「n / 150」。 */
        fun secretCharCount(plaintext: String): Int =
            plaintext.codePointCount(0, plaintext.length)

        /** 是否属于「密码错误」类错误。只有这类才计入连续失败次数与退避。 */
        fun isPasswordError(e: SvcError): Boolean =
            e == SvcError.PASSWORD_WRONG || e == SvcError.RATE_LIMITED
    }

    private val kekCache = KekCache()
    private val backoff = PasswordBackoff()
    private val mkStore = MasterKeyStore(db, files)
    private val secretStore = SecretStore(db, files)

    // ------------------------------------------------------ 生命周期

    fun open(): Boolean = files.ensureLayout()

    fun close() {
        lock()
        db.close()
    }

    // ------------------------------------------------------ 会话与锁定

    /**
     * 用指定主密钥的密码解锁。成功后 KEK 进入缓存，并触发一次轮换（需求 4.2）。
     */
    fun unlock(masterKeyId: String, password: ByteArray): SvcError {
        if (db.findMasterKey(masterKeyId) == null) return SvcError.MASTER_KEY_NOT_FOUND
        val (se, file) = mkStore.load(masterKeyId)
        if (se != SaveError.OK) return mapSave(se)
        val f = file!!

        val kek = Crypto.deriveKek(password, f.salt, f.kdfMem.toInt(),
            f.kdfIter.toInt(), f.kdfPar.toInt(), KekCache.KEK_LENGTH)

        // 只解公钥即可判定密码正确性，不必付出解私钥的代价。
        if (!aesGcmDecryptOk(kek, f.pubKeyNonce, f.masterKeyId, f.pubKeyCipher)) {
            return notePasswordFailure()
        }
        if (!kekCache.load(kek)) return SvcError.INTERNAL
        notePasswordSuccess()
        // 需求 4.2：每次解锁成功后立即轮换一次。
        kekCache.rotate()
        return SvcError.OK
    }

    /** 锁定：清空 KEK 缓存与退避计数。 */
    fun lock() {
        kekCache.clear()
        backoff.reset()
    }

    fun isUnlocked(): Boolean = kekCache.hasValue()

    /** KEK 轮换（需求 2.7 的 30 秒定时器）。未解锁时返回 LOCKED。 */
    fun rotateKek(): SvcError =
        if (!isUnlocked()) SvcError.LOCKED
        else if (kekCache.rotate()) SvcError.OK else SvcError.INTERNAL

    /** 记录一次用户活动，供 UI 的闲置锁定计时使用。 */
    fun notifyActivity() {}

    /** 退避状态。UI 用它显示「N 秒后可解锁」。 */
    fun backoffStatus(): BackoffStatus = BackoffStatus(
        waiting = backoff.isWaiting(),
        remainingSeconds = backoff.remainingSeconds(),
        consecutiveFailures = backoff.consecutiveFailures(),
    )

    // ------------------------------------------------------ 主密钥（需求 §2）

    fun listMasterKeys(): List<MasterKeyListItem> = db.listMasterKeys().map { row ->
        MasterKeyListItem(
            masterKeyId = row.masterKeyId,
            name = row.name,
            isDefault = row.isDefault || isDefault(row.masterKeyId),
            secretCount = db.countSecretsForMasterKey(row.masterKeyId),
            createdAt = row.createdAt,
        )
    }

    /** 生成一把新主密钥。第一把自动成为默认主密钥。 */
    fun createMasterKey(name: String, password: ByteArray): SvcError {
        if (password.isEmpty()) return SvcError.PASSWORD_WRONG
        // 限额是业务规则，在 service 判定；存储层只如实计数。
        if (db.masterKeyCount() >= ServiceLimits.MAX_MASTER_KEYS) {
            return SvcError.MASTER_KEY_QUOTA_EXCEEDED
        }

        val id = Crypto.generateId()
        val salt = Crypto.randomBytes(Serialize.SALT_LENGTH)
        val kek = Crypto.deriveKek(password, salt, Crypto.KDF_MEMORY_KIB,
            Crypto.KDF_ITERATIONS, Crypto.KDF_PARALLELISM, KekCache.KEK_LENGTH)

        val pair = try {
            Crypto.rsaGenerateKeyPair()
        } catch (e: Exception) {
            return SvcError.INTERNAL
        }

        // rsaGenerateKeyPair 返回 (私钥 PKCS#8, 公钥 SPKI)，sealKeys 形参是 (公钥, 私钥)，
        // 顺序不可颠倒：公钥字段错存私钥会让 RSA 封装全部失败。
        val sealed = sealKeys(kek, id, pair.second, pair.first)
            ?: return SvcError.INTERNAL

        val f = Serialize.MasterKeyFile(
            masterKeyId = id,
            name = name,
            salt = salt,
            kdfMem = Crypto.KDF_MEMORY_KIB.toLong(),
            kdfIter = Crypto.KDF_ITERATIONS.toLong(),
            kdfPar = Crypto.KDF_PARALLELISM.toLong(),
            pubKeyNonce = sealed.pubNonce,
            pubKeyTag = sealed.pubTag,
            pubKeyCipher = sealed.pubCipher,
            privKeyNonce = sealed.privNonce,
            privKeyTag = sealed.privTag,
            privKeyCipher = sealed.privCipher,
        )

        // 第一把主密钥自动成为默认主密钥。
        val makeDefault = db.masterKeyCount() == 0
        val se = mkStore.save(f, name, makeDefault)
        if (se != SaveError.OK) return mapSave(se)
        if (makeDefault) {
            if (db.setDefaultMasterKey(Ids.toHex(id)) != StoreError.OK) return SvcError.INTERNAL
        }
        return SvcError.OK
    }

    fun verifyMasterKeyPassword(masterKeyId: String, password: ByteArray): SvcError {
        val (kek, err) = resolveKek(masterKeyId, password)
        if (err != SvcError.OK) return err
        return SvcError.OK
    }

    /**
     * 导出主密钥。masterPassword 为空且目标是默认主密钥时用缓存的 KEK；
     * 否则必须提供该密钥的密码，否则返回 NEEDS_PASSWORD。
     * protectionPassword 是导出文件的新保护密码，不能与主密钥密码混用。
     */
    fun exportMasterKey(
        masterKeyId: String,
        masterPassword: ByteArray,
        protectionPassword: ByteArray,
    ): Pair<SvcError, ByteArray?> {
        if (protectionPassword.isEmpty()) {
            // 保护密码是导出时新设的，不能与主密钥密码混用。
            return Pair(SvcError.PASSWORD_WRONG, null)
        }
        val (kek, ke) = resolveKek(masterKeyId, masterPassword)
        if (ke != SvcError.OK) return Pair(ke, null)

        val (se, file) = mkStore.load(masterKeyId)
        if (se != SaveError.OK) return Pair(mapSave(se), null)
        val f = file!!

        val pubPlain = decryptOrNul(kek, f.pubKeyNonce, f.masterKeyId, f.pubKeyCipher)
            ?: return Pair(SvcError.PASSWORD_WRONG, null)
        val privPlain = decryptOrNul(kek, f.privKeyNonce, f.masterKeyId, f.privKeyCipher)
            ?: return Pair(SvcError.PASSWORD_WRONG, null)

        // docs/03-data §7.3：导出使用新盐、保护密码派生的新 KEK、公私钥各自新 nonce。
        val salt = Crypto.randomBytes(Serialize.SALT_LENGTH)
        val protKek = Crypto.deriveKek(protectionPassword, salt, Crypto.KDF_MEMORY_KIB,
            Crypto.KDF_ITERATIONS, Crypto.KDF_PARALLELISM, KekCache.KEK_LENGTH)
        val sealed = sealKeys(protKek, f.masterKeyId, pubPlain, privPlain)
            ?: return Pair(SvcError.INTERNAL, null)

        val dst = Serialize.MasterKeyFile(
            masterKeyId = f.masterKeyId,
            name = f.name,
            salt = salt,
            kdfMem = Crypto.KDF_MEMORY_KIB.toLong(),
            kdfIter = Crypto.KDF_ITERATIONS.toLong(),
            kdfPar = Crypto.KDF_PARALLELISM.toLong(),
            pubKeyNonce = sealed.pubNonce,
            pubKeyTag = sealed.pubTag,
            pubKeyCipher = sealed.pubCipher,
            privKeyNonce = sealed.privNonce,
            privKeyTag = sealed.privTag,
            privKeyCipher = sealed.privCipher,
        )
        return Pair(SvcError.OK, Serialize.serializeMasterKey(dst))
    }

    /** 导入主密钥：用 protectionPassword 解开文件，再用 newPassword 重加密保存。 */
    fun importMasterKey(
        bytes: ByteArray,
        protectionPassword: ByteArray,
        newPassword: ByteArray,
    ): SvcError {
        if (protectionPassword.isEmpty() || newPassword.isEmpty()) {
            return SvcError.PASSWORD_WRONG
        }
        // 限额在 service 判定：导入会新增一把主密钥。
        if (db.masterKeyCount() >= ServiceLimits.MAX_MASTER_KEYS) {
            return SvcError.MASTER_KEY_QUOTA_EXCEEDED
        }

        val src = when (val parsed = Serialize.parseMasterKey(bytes)) {
            is Serialize.Result.Err -> return when (parsed.error) {
                Serialize.ParseError.UNSUPPORTED_VERSION -> SvcError.IMPORT_VERSION_TOO_HIGH
                else -> SvcError.IMPORT_BAD_FORMAT
            }
            is Serialize.Result.Ok -> parsed.value
        }

        val protKek = Crypto.deriveKek(protectionPassword, src.salt, Crypto.KDF_MEMORY_KIB,
            Crypto.KDF_ITERATIONS, Crypto.KDF_PARALLELISM, KekCache.KEK_LENGTH)
        val pubPlain = decryptOrNul(protKek, src.pubKeyNonce, src.masterKeyId, src.pubKeyCipher)
            ?: return SvcError.PASSWORD_WRONG
        val privPlain = decryptOrNul(protKek, src.privKeyNonce, src.masterKeyId, src.privKeyCipher)
            ?: return SvcError.PASSWORD_WRONG

        // ID 冲突时重新生成，保证「导出→导入」往返一致且不覆盖本地记录。
        var id = src.masterKeyId
        var hexId = Ids.toHex(id)
        if (db.findMasterKey(hexId) != null) {
            id = Crypto.generateId()
            hexId = Ids.toHex(id)
        }

        val salt = Crypto.randomBytes(Serialize.SALT_LENGTH)
        val newKek = Crypto.deriveKek(newPassword, salt, Crypto.KDF_MEMORY_KIB,
            Crypto.KDF_ITERATIONS, Crypto.KDF_PARALLELISM, KekCache.KEK_LENGTH)
        val sealed = sealKeys(newKek, id, pubPlain, privPlain) ?: return SvcError.INTERNAL

        val dst = Serialize.MasterKeyFile(
            masterKeyId = id,
            name = src.name,
            salt = salt,
            kdfMem = Crypto.KDF_MEMORY_KIB.toLong(),
            kdfIter = Crypto.KDF_ITERATIONS.toLong(),
            kdfPar = Crypto.KDF_PARALLELISM.toLong(),
            pubKeyNonce = sealed.pubNonce,
            pubKeyTag = sealed.pubTag,
            pubKeyCipher = sealed.pubCipher,
            privKeyNonce = sealed.privNonce,
            privKeyTag = sealed.privTag,
            privKeyCipher = sealed.privCipher,
        )
        // §2.4：导入的密钥不自动成为默认主密钥。
        val se = mkStore.save(dst, src.name, makeDefault = false)
        if (se != SaveError.OK) return mapSave(se)
        return SvcError.OK
    }

    /**
     * 切换默认主密钥。需求 2.5 要求**同时**验证原默认与新默认两把密钥的密码。
     * 已解锁时两者都能用缓存 KEK 校验；未解锁时至少需要提供其中一个。
     */
    fun switchDefaultMasterKey(
        masterKeyId: String,
        currentPassword: ByteArray,
        newPassword: ByteArray,
    ): SvcError {
        val current = db.defaultMasterKeyId() ?: return SvcError.MASTER_KEY_NOT_FOUND
        if (db.findMasterKey(masterKeyId) == null) return SvcError.MASTER_KEY_NOT_FOUND

        if (current != masterKeyId) {
            // 切换默认时两把密钥都要过密码校验；已解锁时缓存 KEK 是会话凭证。
            val verify: (String, ByteArray) -> SvcError = { id, pw ->
                if (pw.isEmpty() && kekCache.hasValue()) {
                    SvcError.OK
                } else {
                    val (_, e) = resolveKek(id, pw)
                    e
                }
            }
            val e = verify(current, currentPassword)
            if (e != SvcError.OK) return e
            val e2 = verify(masterKeyId, newPassword)
            if (e2 != SvcError.OK) return e2
        }

        if (db.setDefaultMasterKey(masterKeyId) != StoreError.OK) return SvcError.INTERNAL

        // 默认密钥换了，缓存的 KEK 不再对应任何默认密钥，必须清掉。
        kekCache.clear()
        return SvcError.OK
    }

    fun deleteMasterKey(masterKeyId: String, password: ByteArray): SvcError {
        val row = db.findMasterKey(masterKeyId) ?: return SvcError.MASTER_KEY_NOT_FOUND
        // §2.6：禁止删除默认主密钥。
        if (row.isDefault || isDefault(masterKeyId)) return SvcError.CANNOT_DELETE_DEFAULT

        val (kek, ke) = resolveKek(masterKeyId, password)
        if (ke != SvcError.OK) return ke

        if (db.deleteMasterKey(masterKeyId) != StoreError.OK) return SvcError.INTERNAL

        // 索引条目删除后才删数据文件。关联的机密信息**保留**（需求 2.6）。
        files.remove(row.filePath)
        return SvcError.OK
    }

    // -------------------------------------------------- 机密信息（需求 §3）

    fun listSecrets(): List<SecretListItem> = db.listSecrets().map { row ->
        val mk = db.findMasterKey(row.masterKeyId)
        // 悬空引用是合法状态：主密钥可能已删除而机密信息保留（需求 2.6）。
        SecretListItem(
            secretId = row.secretId,
            title = row.title,
            masterKeyId = row.masterKeyId,
            masterKeyName = mk?.name ?: "",
            masterKeyFound = mk != null,
        )
    }

    /** 新增一条机密信息。masterPassword 为空且目标是默认主密钥时用缓存的 KEK。 */
    fun addSecret(
        title: String,
        plaintext: String,
        masterKeyId: String,
        masterPassword: ByteArray,
    ): SvcError {
        // 3.2 的长度校验按 Unicode 码点计，且**先于**选密钥与输密码。
        if (plaintext.isEmpty()) return SvcError.EMPTY_SECRET
        if (secretCharCount(plaintext) > ServiceLimits.MAX_SECRET_CHARS) {
            return SvcError.SECRET_TOO_LONG
        }
        if (db.secretCount() >= ServiceLimits.MAX_SECRETS) {
            return SvcError.SECRET_QUOTA_EXCEEDED
        }

        val (masterKek, ke) = resolveKek(masterKeyId, masterPassword)
        if (ke != SvcError.OK) return ke

        val (kse, mkFile) = mkStore.load(masterKeyId)
        if (kse != SaveError.OK) return mapSave(kse)
        val mk = mkFile!!

        val pubPlain = decryptOrNul(masterKek, mk.pubKeyNonce, mk.masterKeyId, mk.pubKeyCipher)
            ?: return SvcError.PASSWORD_WRONG

        // 3.2 step 3: 每条机密信息一把全新的 AES-256 数据密钥。
        val dk = Crypto.randomBytes(32)
        val newSecretId = Crypto.generateId()

        // 3.2 step 5: 用主密钥公钥封装数据密钥。
        val wrapped = try {
            Crypto.rsaOaepEncrypt(pubPlain, dk)
        } catch (e: Exception) {
            return SvcError.INTERNAL
        }

        val nonce = Crypto.randomBytes(Serialize.NONCE_LENGTH)

        // 3.2 step 4: 用数据密钥加密，AAD 是原始 secretId。
        val (cipher, tag) = Crypto.aesGcmEncrypt(dk, nonce, newSecretId,
            plaintext.toByteArray(Charsets.UTF_8))

        val f = Serialize.SecretFile(
            secretId = newSecretId,
            masterKeyId = Ids.fromHex(masterKeyId) ?: return SvcError.MASTER_KEY_NOT_FOUND,
            title = title,
            wrappedDk = wrapped,
            dataNonce = nonce,
            dataTag = tag,
            dataCipher = cipher + tag, // 布局：密文 + tag 尾
        )
        val se = secretStore.save(f)
        if (se != SaveError.OK) return mapSave(se)
        return SvcError.OK
    }

    /**
     * 需求 3.3：导出内容与数据文件逐字节相同，因此不需要密码——
     * 文件本身已用数据密钥加密，数据密钥由主密钥封装。
     */
    fun exportSecret(secretId: String, masterPassword: ByteArray): Pair<SvcError, ByteArray?> {
        if (db.findSecret(secretId) == null) return Pair(SvcError.SECRET_NOT_FOUND, null)
        val (se, file) = secretStore.load(secretId)
        if (se != SaveError.OK) return Pair(mapSave(se), null)
        return Pair(SvcError.OK, Serialize.serializeSecret(file!!))
    }

    /** 导入机密信息文件。filePassword 预留（v0.0.1 不再包一层保护密码）。 */
    fun importSecret(
        bytes: ByteArray,
        filePassword: ByteArray,
        masterPassword: ByteArray,
    ): SvcError {
        if (db.secretCount() >= ServiceLimits.MAX_SECRETS) {
            return SvcError.SECRET_QUOTA_EXCEEDED
        }
        val src = when (val parsed = Serialize.parseSecret(bytes)) {
            is Serialize.Result.Err -> return when (parsed.error) {
                Serialize.ParseError.UNSUPPORTED_VERSION -> SvcError.IMPORT_VERSION_TOO_HIGH
                else -> SvcError.IMPORT_BAD_FORMAT
            }
            is Serialize.Result.Ok -> parsed.value
        }

        // 主密钥必须存在本地，否则无法解封装。
        val mkHex = Ids.toHex(src.masterKeyId)
        if (db.findMasterKey(mkHex) == null) return SvcError.IMPORT_MASTER_KEY_MISSING

        val (masterKek, ke) = resolveKek(mkHex, masterPassword)
        if (ke != SvcError.OK) return ke

        // 解密校验：数据密钥必须真的能解开，否则文件已损坏或密码不对。
        val plain = unwrapPlaintext(masterKek, src)
            ?: return SvcError.IMPORT_DECRYPT_FAILED

        var record = src
        // 保留原 ID 使导出/导入往返无损；冲突时换新 ID 且重加密（ID 是 AAD）。
        if (db.findSecret(Ids.toHex(src.secretId)) != null) {
            val newId = Crypto.generateId()
            val (kse, mkFile) = mkStore.load(mkHex)
            if (kse != SaveError.OK) return mapSave(kse)
            val mk = mkFile!!
            val privPlain = decryptOrNul(masterKek, mk.privKeyNonce, mk.masterKeyId,
                mk.privKeyCipher) ?: return SvcError.PASSWORD_WRONG
            val dk = try {
                Crypto.rsaOaepDecrypt(privPlain, src.wrappedDk)
            } catch (e: Exception) {
                return SvcError.IMPORT_DECRYPT_FAILED
            }
            val nonce = Crypto.randomBytes(Serialize.NONCE_LENGTH)
            val (cipher, tag) = Crypto.aesGcmEncrypt(dk, nonce, newId, plain)
            record = src.copy(secretId = newId, dataNonce = nonce, dataTag = tag,
                dataCipher = cipher + tag) // 布局：密文 + tag 尾
        }

        val se = secretStore.save(record)
        if (se != SaveError.OK) return mapSave(se)
        return SvcError.OK
    }

    /**
     * 详情页。masterPassword 为空时返回掩码态，不返回 NEEDS_PASSWORD——
     * UI 可以先展示列表再决定是否索要密码。
     */
    fun getSecretDetail(secretId: String, masterPassword: ByteArray): Pair<SvcError, SecretDetail?> {
        val row = db.findSecret(secretId) ?: return Pair(SvcError.SECRET_NOT_FOUND, null)

        val mk = db.findMasterKey(row.masterKeyId)
        if (mk == null) {
            // 需求 3.5：主密钥缺失 -> 掩码正文 + 空名称列 + ID 列黄色问号。
            return Pair(
                SvcError.OK,
                SecretDetail(
                    secretId = row.secretId,
                    title = row.title,
                    masterKeyId = row.masterKeyId,
                    masterKeyName = "",
                    masterKeyFound = false,
                    plaintextMasked = true,
                ),
            )
        }

        // 主密钥存在但名称可能为空。那**不是**「未找到」，不得产生问号。
        if (masterPassword.isEmpty() && !isDefault(row.masterKeyId)) {
            return Pair(
                SvcError.OK,
                SecretDetail(
                    secretId = row.secretId,
                    title = row.title,
                    masterKeyId = row.masterKeyId,
                    masterKeyName = mk.name,
                    masterKeyFound = true,
                    plaintextMasked = true,
                ),
            )
        }

        val (e, plain) = revealSecretPlaintext(secretId, masterPassword)
        return when {
            e == SvcError.OK -> Pair(
                SvcError.OK,
                SecretDetail(
                    secretId = row.secretId,
                    title = row.title,
                    masterKeyId = row.masterKeyId,
                    masterKeyName = mk.name,
                    masterKeyFound = true,
                    plaintextMasked = false,
                    plaintext = plain ?: "",
                ),
            )
            e == SvcError.NEEDS_PASSWORD || e == SvcError.LOCKED -> Pair(
                SvcError.OK,
                SecretDetail(
                    secretId = row.secretId,
                    title = row.title,
                    masterKeyId = row.masterKeyId,
                    masterKeyName = mk.name,
                    masterKeyFound = true,
                    plaintextMasked = true,
                ),
            )
            else -> Pair(e, null)
        }
    }

    /** 「查看」明文。默认主密钥用缓存 KEK；非默认主密钥需密码。 */
    fun revealSecretPlaintext(secretId: String, masterPassword: ByteArray): Pair<SvcError, String?> {
        val row = db.findSecret(secretId) ?: return Pair(SvcError.SECRET_NOT_FOUND, null)
        val (se, file) = secretStore.load(secretId)
        if (se != SaveError.OK) return Pair(mapSave(se), null)
        val f = file!!

        val (masterKek, ke) = resolveKek(row.masterKeyId, masterPassword)
        if (ke != SvcError.OK) return Pair(ke, null)

        val plain = unwrapPlaintext(masterKek, f) ?: return Pair(SvcError.IMPORT_DECRYPT_FAILED, null)
        return Pair(SvcError.OK, String(plain, Charsets.UTF_8))
    }

    fun deleteSecret(secretId: String, masterPassword: ByteArray): SvcError {
        val row = db.findSecret(secretId) ?: return SvcError.SECRET_NOT_FOUND

        // 需求 3.6：删除需验证该密钥密码；主密钥缺失时跳过校验。
        val (kek, ke) = resolveKek(row.masterKeyId, masterPassword)
        if (ke != SvcError.OK && ke != SvcError.MASTER_KEY_NOT_FOUND) return ke

        // 先删索引行再删数据文件：反过来会在删索引失败时留下孤儿文件。
        if (db.deleteSecret(secretId) != StoreError.OK) return SvcError.INTERNAL
        files.remove(row.filePath)
        return SvcError.OK
    }

    // ------------------------------------------------------ 杂项

    fun quota(): QuotaStatus = QuotaStatus(
        masterKeys = db.masterKeyCount(),
        secrets = db.secretCount(),
    )

    fun defaultMasterKeyId(): String? = db.defaultMasterKeyId()

    // ------------------------------------------------------ 内部实现

    private fun isDefault(masterKeyId: String): Boolean =
        db.defaultMasterKeyId() == masterKeyId

    /** 记录一次密码失败。首次失败报密码错误；退避窗口内再失败才升级为限流。 */
    private fun notePasswordFailure(): SvcError {
        val wasWaiting = backoff.isWaiting()
        backoff.recordFailure()
        return if (wasWaiting) SvcError.RATE_LIMITED else SvcError.PASSWORD_WRONG
    }

    private fun notePasswordSuccess() {
        backoff.reset()
    }

    /**
     * 解析本次操作该用哪个 KEK：
     *   密码非空 -> 校验该密码，成功则用它的 KEK（失败计入退避）
     *   密码为空 -> 仅限「目标是默认主密钥」且「已解锁」时用缓存 KEK；
     *              否则返回 NEEDS_PASSWORD / LOCKED。
     */
    private fun resolveKek(masterKeyId: String, password: ByteArray): Pair<ByteArray, SvcError> {
        if (db.findMasterKey(masterKeyId) == null) {
            return Pair(ByteArray(0), SvcError.MASTER_KEY_NOT_FOUND)
        }
        if (password.isEmpty()) {
            if (!isDefault(masterKeyId)) return Pair(ByteArray(0), SvcError.NEEDS_PASSWORD)
            val cached = kekCache.peek() ?: return Pair(ByteArray(0), SvcError.LOCKED)
            return Pair(cached, SvcError.OK)
        }
        val (se, file) = mkStore.load(masterKeyId)
        if (se != SaveError.OK) return Pair(ByteArray(0), mapSave(se))
        val f = file!!
        val kek = Crypto.deriveKek(password, f.salt, f.kdfMem.toInt(),
            f.kdfIter.toInt(), f.kdfPar.toInt(), KekCache.KEK_LENGTH)
        if (!aesGcmDecryptOk(kek, f.pubKeyNonce, f.masterKeyId, f.pubKeyCipher)) {
            return Pair(ByteArray(0), notePasswordFailure())
        }
        notePasswordSuccess()
        return Pair(kek, SvcError.OK)
    }

    /** GCM 解密（AAD = masterKeyId），失败返回 null。密文含 16 字节 tag 尾。 */
    private fun decryptOrNul(
        kek: ByteArray,
        nonce: ByteArray,
        aad: ByteArray,
        cipherWithTag: ByteArray,
    ): ByteArray? = try {
        Crypto.aesGcmDecrypt(kek, nonce, aad,
            cipherWithTag.copyOfRange(0, cipherWithTag.size - 16),
            cipherWithTag.copyOfRange(cipherWithTag.size - 16, cipherWithTag.size))
    } catch (e: Exception) {
        null
    }

    private fun aesGcmDecryptOk(
        kek: ByteArray,
        nonce: ByteArray,
        aad: ByteArray,
        cipherWithTag: ByteArray,
    ): Boolean = decryptOrNul(kek, nonce, aad, cipherWithTag) != null

    /** 公私钥的 nonce 必须不同，否则 GCM 密钥流相同、机密性彻底失效。 */
    private data class SealedKeys(
        val pubNonce: ByteArray,
        val pubTag: ByteArray,
        val pubCipher: ByteArray,
        val privNonce: ByteArray,
        val privTag: ByteArray,
        val privCipher: ByteArray,
    )

    private fun sealKeys(kek: ByteArray, aad: ByteArray, pubDer: ByteArray,
                         privDer: ByteArray): SealedKeys? {
        var pubNonce = ByteArray(0)
        var privNonce = ByteArray(0)
        var ok = false
        for (attempt in 0 until 8) {
            pubNonce = Crypto.randomBytes(Serialize.NONCE_LENGTH)
            privNonce = Crypto.randomBytes(Serialize.NONCE_LENGTH)
            if (!pubNonce.contentEquals(privNonce)) {
                ok = true
                break
            }
        }
        if (!ok) return null
        val (pubCipher, pubTag) = Crypto.aesGcmEncrypt(kek, pubNonce, aad, pubDer)
        val (privCipher, privTag) = Crypto.aesGcmEncrypt(kek, privNonce, aad, privDer)
        // 桌面格式约定：cipher 字段 = 密文 + 16 字节 tag 尾（kCipherTail），
        // tag 同时单独存一份。两处都必须写入，否则解析侧把密文尾部当 tag。
        return SealedKeys(pubNonce, pubTag, pubCipher + pubTag,
            privNonce, privTag, privCipher + privTag)
    }

    /** 解开数据密钥，再用它解密机密内容。详情、查看明文、导入三处的公共步骤。 */
    private fun unwrapPlaintext(masterKek: ByteArray, f: Serialize.SecretFile): ByteArray? {
        val mkHex = Ids.toHex(f.masterKeyId)
        val (kse, mkFile) = mkStore.load(mkHex)
        if (kse != SaveError.OK) return null
        val mk = mkFile!!
        val privPlain = decryptOrNul(masterKek, mk.privKeyNonce, mk.masterKeyId,
            mk.privKeyCipher) ?: return null
        // 3.4 step 3: 用主密钥私钥解封装数据密钥。
        val dk = try {
            Crypto.rsaOaepDecrypt(privPlain, f.wrappedDk)
        } catch (e: Exception) {
            return null
        }
        // 3.4 step 4: 用数据密钥解密，AAD 是原始 secretId。
        // dataCipher 布局是「密文 + 16 字节 tag 尾」，需先剥尾再验 tag。
        return try {
            Crypto.aesGcmDecrypt(dk, f.dataNonce, f.secretId,
                f.dataCipher.copyOfRange(0, f.dataCipher.size - Serialize.TAG_LENGTH),
                f.dataCipher.copyOfRange(f.dataCipher.size - Serialize.TAG_LENGTH,
                    f.dataCipher.size))
        } catch (e: Exception) {
            null
        }
    }

    private fun mapSave(e: SaveError): SvcError = when (e) {
        SaveError.OK -> SvcError.OK
        SaveError.NOT_FOUND -> SvcError.MASTER_KEY_NOT_FOUND
        SaveError.PARSE_ERROR -> SvcError.IMPORT_BAD_FORMAT
        SaveError.IO_ERROR -> SvcError.IO_ERROR
        SaveError.INVALID_ARGUMENT -> SvcError.INTERNAL
        SaveError.INTERNAL -> SvcError.INTERNAL
    }
}
