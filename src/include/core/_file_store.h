#pragma once

// 机密心 - 数据文件读写
//
// 索引库只存路径，字节本体在这里读写。目录布局对应 docs/03-data §6.1：
//   <data_dir>/secret.db        索引库（由 index_db 管理）
//   <data_dir>/keys/<id>.smk   主密钥数据文件
//   <data_dir>/secrets/<id>.ssc 机密信息数据文件
//
// 文件名完全由 ID 派生，不含随机性，便于人工排查与备份。

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/_hex.h"

namespace secretkeeper::store {

class FileStore {
 public:
  FileStore() = default;
  explicit FileStore(std::string data_dir) : data_dir_(std::move(data_dir)) {}

  const std::string& data_dir() const noexcept { return data_dir_; }
  void set_data_dir(std::string dir) { data_dir_ = std::move(dir); }

  // 建目录并做基本的可写性自检。已存在也算成功。
  bool ensure_layout(std::string* error) const;

  // 主密钥数据文件的相对路径，形如 keys/<32hex>.smk。
  static std::string master_key_rel_path(std::string_view master_key_id_hex);
  static std::string secret_rel_path(std::string_view secret_id_hex);

  bool master_key_path(std::string_view master_key_id_hex, std::string* out) const;
  bool secret_path(std::string_view secret_id_hex, std::string* out) const;

  // 写入采用「先写临时文件再原子改名」，避免崩溃或断电留下半截文件
  // 被后续读取当成合法记录。
  bool write_atomic(const std::string& rel_path, std::span<const std::uint8_t> bytes,
                    std::string* error) const;
  bool read(const std::string& rel_path, std::vector<std::uint8_t>* out,
            std::string* error) const;
  bool remove(const std::string& rel_path, std::string* error) const;

 private:
  bool ensure_dir(const std::string& path, std::string* error) const;

  std::string data_dir_;
};

}  // namespace secretkeeper::store
