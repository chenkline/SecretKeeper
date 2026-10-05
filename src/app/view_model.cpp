#include "view_model.h"

#include <string>

namespace secretkeeper::ui {
namespace {

// 缺失主密钥时追加的问号。UI 用它决定该列的着色。
constexpr const char* kQuestionMark = " ?";

std::string yes_no(bool v) { return v ? "\xE6\x98\xAF" : ""; }

}  // namespace

std::vector<Row> project_master_keys(
    const std::vector<service::MasterKeyListItem>& items) {
  std::vector<Row> rows;
  rows.reserve(items.size());
  for (const service::MasterKeyListItem& item : items) {
    rows.push_back(Row{item.master_key_id, item.name, yes_no(item.is_default), false});
  }
  return rows;
}

std::vector<Row> project_secrets(const std::vector<service::SecretListItem>& items) {
  std::vector<Row> rows;
  rows.reserve(items.size());
  for (const service::SecretListItem& item : items) {
    rows.push_back(Row{item.title,
                       master_key_id_cell(item.master_key_id, item.master_key_found),
                       item.master_key_name,
                       !item.master_key_found});
  }
  return rows;
}

std::string master_key_id_cell(std::string_view master_key_id, bool found) {
  std::string cell(master_key_id);
  if (!found) cell += kQuestionMark;
  return cell;
}

std::string error_text(service::Error e) {
  const std::string_view base = service::message(e);
  return std::string(base);
}

std::string backoff_text(const service::BackoffStatus& status) {
  if (!status.waiting) return {};
  return std::string(service::message(service::Error::kRateLimited)) +
         std::to_string(status.remaining_seconds) + "\xE7\xA7\x92";
}

std::string char_counter_text(std::string_view plaintext) {
  return std::to_string(service::Service::secret_char_count(plaintext)) + " / " +
         std::to_string(service::kMaxSecretChars);
}

}  // namespace secretkeeper::ui
