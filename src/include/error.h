#pragma once

// 机密心 - 业务层错误与提示文案
//
// 需求要求**任何密码输入错误统一提示"主密钥密码错误"**，不区分底层失败原因
// （GCM 认证失败、文件损坏、密码错误在外部看来都是同一个提示）。
// 但业务层内部仍需区分，否则无法决定下一步动作（例如导入时是"主密钥缺失"
// 还是"解密失败"）。
//
// 因此这里分两层：
//   1. 内部错误码（Error）—— 供业务逻辑判断分支
//   2. 提示文案（message(Error)）—— 直接对应需求文档中的固定措辞
//
// 提示文案集中在此，UI 层只负责显示，不得自行拼装或改写措辞。

#include <string_view>

namespace secretkeeper::core {

enum class Error {
  kOk = 0,

  // 密码与解锁
  kPasswordWrong,            // 主密钥密码错误
  kRateLimited,              // 退避等待中（由调用方补充剩余秒数）

  // 主密钥
  kMasterKeyQuotaExceeded,   // 主密钥数量已达上限
  kMasterKeyIsDefault,       // 禁止删除默认主密钥
  kMasterKeyNotFound,        // 主密钥不存在
  kCannotDeleteDefault,      // 不能删除默认主密钥

  // 机密信息
  kSecretQuotaExceeded,      // 机密信息条数已达上限
  kSecretTooLong,            // 超出 150 字符
  kSecretNotFound,
  kEmptySecret,              // 内容为空

  // 导入导出
  kImportBadFormat,          // 格式非法 / Magic 不匹配
  kImportVersionTooHigh,     // 文件版本过高
  kImportMasterKeyMissing,   // 请先导入主密钥
  kImportDecryptFailed,      // 解密错误
  kImportSaveFailed,         // 保存出错
  kImportConflict,           // ID 冲突（内部保留，一般自动改 ID）

  // 环境与存储
  kIoError,
  kCryptoUnsupported,        // 环境缺少所需算法能力
  kInternal,
};

// 需求文档中固定的中文提示文案。UI 必须原样显示，不得改写。
// 生命周期为 static，调用方无需管理。
std::string_view message(Error e);

// 是否属于「密码错误」类错误。用于退避计数——只有密码错误才计入连续失败次数。
bool is_password_error(Error e);

}  // namespace secretkeeper::core
