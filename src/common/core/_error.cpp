#include "core/_error.h"

namespace secretkeeper::service {

std::string_view message(Error e) {
  switch (e) {
    case Error::kOk:
      return "";

    case Error::kPasswordWrong:
      return "主密钥密码错误";

    case Error::kNeedsPassword:
      return "请输入该主密钥密码";

    case Error::kLocked:
      return "请先解锁";

    case Error::kRateLimited:
      // 剩余秒数由 UI 自行附加，这里只给前缀。
      return "请稍候再试，剩余等待 ";

    case Error::kMasterKeyQuotaExceeded:
      return "请先导出并删除部分主密钥后再导入";

    case Error::kMasterKeyIsDefault:
    case Error::kCannotDeleteDefault:
      return "默认主密钥不能删除，请先切换默认主密钥";

    case Error::kMasterKeyNotFound:
      return "主密钥不存在";

    case Error::kSecretQuotaExceeded:
      return "机密信息条数已达上限，请先导出并删除部分机密信息";

    case Error::kSecretTooLong:
      return "机密信息超过 150 字符，请精简后重试";

    case Error::kSecretNotFound:
      return "机密信息不存在";

    case Error::kEmptySecret:
      return "机密信息不能为空";

    case Error::kImportBadFormat:
      return "导入失败，请选择正确的导出文件";

    case Error::kImportVersionTooHigh:
      return "导入失败，文件版本过高";

    case Error::kImportMasterKeyMissing:
      return "导入失败，请先导入主密钥";

    case Error::kImportDecryptFailed:
      return "导入失败，解密错误";

    case Error::kImportSaveFailed:
      return "导入失败，保存出错";

    case Error::kImportConflict:
      return "导入失败，保存出错";

    case Error::kIoError:
      return "文件读写失败";

    case Error::kCryptoUnsupported:
      return "当前环境缺少必要的加密能力";

    case Error::kInternal:
      return "内部错误";
  }
  return "未知错误";
}

const char* const kMaskedPlaintext = "******";

bool is_password_error(Error e) {
  // 只有「用户输入的密码/保护密码不对」才计入退避。
  // 文件损坏、格式非法属于数据问题，不该让用户干等。
  return e == Error::kPasswordWrong;
}

}  // namespace secretkeeper::service
