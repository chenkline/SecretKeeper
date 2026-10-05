#pragma once

// SecretKeeper - UI 视图模型（纯逻辑）
//
// 这里承载**不需要 FLTK 事件循环**就能确定的界面逻辑：列表行投影、
// 黄色问号判定、错误码到提示文案的映射、退避剩余时间格式化、字符计数。
//
// 抽出这一层有两个原因：
//   1. 这些规则是需求硬约束（尤其黄色问号），必须有自动测试守着；
//   2. app.cpp 只剩控件搭建与事件绑定，不掺杂可测的业务判断。
//
// 本文件只依赖 service 层，不得包含任何 FLTK 头。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/service.h"

namespace secretkeeper::ui {

// 一行列表数据：三列文本 + 该行是否要显示黄色问号。
struct Row {
  std::string c0;
  std::string c1;
  std::string c2;
  bool show_question_mark = false;
};

// 主密钥列表投影：三列为主密钥 ID、名称、默认标记。
std::vector<Row> project_master_keys(
    const std::vector<service::MasterKeyListItem>& items);

// 机密信息列表投影：三列为标题、主密钥 ID、主密钥名称。
//
// 黄色问号**只**由 master_key_found 决定，与 master_key_name 是否为空
// 完全无关：主密钥存在但名称为空 -> 无问号；主密钥缺失 -> 有问号。
// 两种「名称为空」的情形必须能被界面区分开。
std::vector<Row> project_secrets(const std::vector<service::SecretListItem>& items);

// 主密钥 ID 列的最终显示值：缺失时在 ID 后追加一个黄色问号。
std::string master_key_id_cell(std::string_view master_key_id, bool found);

// 错误提示文案。直接透传 service 的固定文案，不做任何改写或补充。
std::string error_text(service::Error e);

// 「请稍候再试，剩余等待 N 秒」；不在退避中时返回空串。
std::string backoff_text(const service::BackoffStatus& status);

// 「n / 150」形式的机密信息长度指示。
std::string char_counter_text(std::string_view plaintext);

}  // namespace secretkeeper::ui
