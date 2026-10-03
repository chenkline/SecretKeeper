#pragma once

// 机密心 - 默认主密钥 KEK 缓存
//
// 需求 2.7：
//   1. 程序启动时生成随机对称密钥（**程序随机密钥**）
//   2. 用程序随机密钥以 AES-256-GCM 加密默认主密钥 KEK，得到 KEK 密文
//   3. KEK 密文与程序随机密钥**分散保存**在内存的不同结构中，
//      不存在同时包含两者的连续对象
//   4. 内存中为 KEK 密文准备 **3 个备选槽位**。每次重新加密时随机迁移到
//      另一个槽位，**原槽位不清除**（作为诱饵）
//   5. **轮换时机**：每 30 秒定时器、**每次解锁成功后**、每次从后台回到前台
//   6. 锁定时立即清零所有槽位与程序随机密钥
//
// 该机制提高内存转储的直接攻击成本，但不替代锁屏密码——这一点在需求中
// 已明确，实现时不应把它当作安全边界的替代品。

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "crypto.h"

namespace secretkeeper::core {

// 需求规定 3 个槽位，不多不少：多则浪费内存并扩大转储窗口，少则失去诱饵意义。
inline constexpr std::size_t kKekSlotCount = 3;
inline constexpr std::uint32_t kKekRotationSeconds = 30;

class KekCache {
 public:
  KekCache() = default;
  KekCache(const KekCache&) = delete;
  KekCache& operator=(const KekCache&) = delete;
  ~KekCache();

  // 生成新的程序随机密钥并载入 KEK。调用前若有旧密钥，会先清零。
  // 每次解锁成功后都应调用一次（需求 4.2）。
  bool load(const crypto::Kek& kek);

  // 轮换：用同一个程序随机密钥重新加密 KEK 密文，并随机迁移到另一个槽位。
  // 返回实际使用的槽位下标。未载入 KEK 时返回 nullopt。
  std::optional<std::size_t> rotate();

  // 锁定：清零程序随机密钥与全部槽位。
  void clear();

  // 取回缓存的 KEK。仅在已载入时有效。
  std::optional<crypto::Kek> peek() const;

  bool has_value() const noexcept { return loaded_; }
  // 当前活跃槽位下标；未载入时返回 nullopt。
  std::optional<std::size_t> active_slot() const noexcept;
  std::size_t slot_count() const noexcept { return slots_.size(); }
  // 某个槽位是否已被占用（用于验证诱饵槽位确实留有数据）。
  bool slot_occupied(std::size_t index) const noexcept;

 private:
  struct Slot {
    crypto::Nonce nonce{};
    crypto::Tag tag{};
    std::vector<std::uint8_t> cipher;
    bool occupied = false;
  };

  // 程序随机密钥与 KEK 密文分处两个不同的成员数组，内存上不相邻连续存放。
  std::array<Slot, kKekSlotCount> slots_{};
  std::array<std::uint8_t, crypto::kKekLength> session_key_{};
  crypto::Kek plain_{};
  bool session_key_set_ = false;
  std::size_t active_ = 0;
  bool loaded_ = false;

  std::optional<std::size_t> rotate_into(std::size_t slot_index, const crypto::Kek& kek);
};

}  // namespace secretkeeper::core
