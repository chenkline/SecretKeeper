import Foundation
import SecretCore

/// UI 状态与业务入口（与 Android AppViewModel.kt 同构）。
/// UI 只通过 service 访问业务，不接触 crypto/serialize/store 的任何符号。
final class AppViewModel: ObservableObject {
    let service: Service

    @Published var unlocked = false
    @Published var masterKeys: [MasterKeyListItem] = []
    @Published var secrets: [SecretListItem] = []
    @Published var quotaText = ""
    @Published var unlockError: String?
    @Published var backoff = BackoffStatus()
    @Published var defaultKeyName = ""
    @Published var defaultKeyId = ""
    @Published var formError: String?
    @Published var detail: SecretDetail?
    @Published var detailError: String?

    private var detailPassword = ""
    /// 详情页导出请求（UI 观察后触发分享面板/落盘）。
    @Published var requestSecretExport: String?

    init(dataDir: String) {
        let db = try! IndexDb(path: (dataDir as NSString).appendingPathComponent("secret.db"))
        db.createSchema()
        service = Service(db: db, files: FileStore(dataDir: dataDir))
        service.open()
        refreshLists()
    }

    func refreshLists() {
        masterKeys = service.listMasterKeys()
        secrets = service.listSecrets()
        let q = service.quota()
        quotaText = "\(q.secrets) / \(q.maxSecrets) 条 · 基础版"
        let defId = service.defaultMasterKeyId()
        defaultKeyId = defId ?? ""
        defaultKeyName = masterKeys.first { $0.masterKeyId == defId }?.name ?? ""
    }

    /// 解锁。失败文案取需求固定文案；退避剩余秒数刷新到状态。
    func unlock(_ masterKeyId: String, _ password: String) {
        let err = service.unlock(masterKeyId, Array(password.utf8))
        backoff = service.backoffStatus()
        if err == .ok {
            unlockError = nil
            unlocked = true
            refreshLists()
        } else {
            let base = SvcMessages.message(err)
            unlockError = backoff.waiting && backoff.remainingSeconds > 0
                ? "\(base)，请 \(backoff.remainingSeconds) 秒后重试"
                : base
        }
    }

    func lock() {
        service.lock()
        unlocked = false
        refreshLists()
    }

    // ------------------------------------------------ 表单（画板 12/17）

    /// 生成主密钥。成功返回 true（UI 返回列表页并刷新）。
    @discardableResult
    func createMasterKey(_ name: String, _ pw: String) -> Bool {
        let err = service.createMasterKey(name, Array(pw.utf8))
        formError = err == .ok ? nil : SvcMessages.message(err)
        if err == .ok { refreshLists() }
        return err == .ok
    }

    /// 新增机密信息。成功返回 true。
    @discardableResult
    func addSecret(_ title: String, _ content: String, _ mkId: String, _ pw: String) -> Bool {
        let err = service.addSecret(title, content, mkId, Array(pw.utf8))
        formError = err == .ok ? nil : SvcMessages.message(err)
        if err == .ok { refreshLists() }
        return err == .ok
    }

    // ------------------------------------------------ 详情（画板 15/16）

    /// 加载详情。masterPassword 为空时返回掩码态（不报错）。
    func loadDetail(_ secretId: String, masterPassword: String = "") {
        detailPassword = masterPassword
        let (e, d) = service.getSecretDetail(secretId, Array(masterPassword.utf8))
        detailError = e == .ok ? nil : SvcMessages.message(e)
        detail = d
    }

    /// 「查看」：带密码重新取明文。
    func revealDetail(_ secretId: String, _ password: String) {
        detailPassword = password
        let (e, plain) = service.revealSecretPlaintext(secretId, Array(password.utf8))
        if e == .ok {
            detailError = nil
            detail = detail.map { $0.with(plaintext: plain ?? "", masked: false) }
        } else {
            detailError = SvcMessages.message(e)
        }
    }

    /// 切换界面 / 进入后台立即隐藏明文（需求 3.5）。
    func maskDetail() {
        detail = detail.map { $0.with(plaintext: "", masked: true) }
    }

    /// 删除机密信息。主密钥缺失时跳过密码校验（与桌面一致）。
    @discardableResult
    func deleteSecret(_ secretId: String, _ password: String) -> Bool {
        let e = service.deleteSecret(secretId, Array(password.utf8))
        if e == .ok {
            refreshLists()
            detail = nil
            return true
        }
        detailError = SvcMessages.message(e)
        return false
    }

    // --------------------------------- 主密钥操作（需求 2.5/2.6）

    /// 切换默认主密钥（两把密钥的密码都需验证；已解锁时可留空用缓存 KEK）。
    @discardableResult
    func switchDefault(_ targetId: String, _ currentPw: String, _ newPw: String) -> Bool {
        let e = service.switchDefaultMasterKey(targetId,
                                               Array(currentPw.utf8), Array(newPw.utf8))
        formError = e == .ok ? nil : SvcMessages.message(e)
        if e == .ok {
            unlocked = false
            refreshLists()
        }
        return e == .ok
    }

    /// 删除非默认主密钥（默认禁止删除由 service 判定）。
    @discardableResult
    func deleteMasterKey(_ masterKeyId: String, _ password: String) -> Bool {
        let e = service.deleteMasterKey(masterKeyId, Array(password.utf8))
        formError = e == .ok ? nil : SvcMessages.message(e)
        if e == .ok { refreshLists() }
        return e == .ok
    }

    // --------------------------------- 导入导出（画板 13/18/19）

    @discardableResult
    func importMasterKey(_ bytes: [UInt8], _ protectionPw: String, _ newPw: String) -> Bool {
        let e = service.importMasterKey(bytes, Array(protectionPw.utf8), Array(newPw.utf8))
        formError = e == .ok ? nil : SvcMessages.message(e)
        if e == .ok { refreshLists() }
        return e == .ok
    }

    @discardableResult
    func importSecret(_ bytes: [UInt8], _ masterPw: String) -> Bool {
        let e = service.importSecret(bytes, [], Array(masterPw.utf8))
        formError = e == .ok ? nil : SvcMessages.message(e)
        if e == .ok { refreshLists() }
        return e == .ok
    }

    /// 导出主密钥字节流（保护密码新设，不能与主密钥密码混用）。
    func exportMasterKey(_ masterKeyId: String, _ protectionPw: String) -> (SvcError, [UInt8]?) {
        service.exportMasterKey(masterKeyId, [], Array(protectionPw.utf8))
    }

    /// 导出机密信息：与数据文件逐字节相同，不需要密码（需求 3.3）。
    func exportSecret(_ secretId: String) -> (SvcError, [UInt8]?) {
        service.exportSecret(secretId, [])
    }
}

extension SecretDetail {
    func with(plaintext: String, masked: Bool) -> SecretDetail {
        var copy = self
        copy.plaintext = plaintext
        copy.plaintextMasked = masked
        return copy
    }
}
