#pragma once

// 机密心 - 密码错误指数退避
//
// 需求 4.5：
//   - 记录连续密码错误次数（**仅存内存**，不落盘）
//   - 第 n 次失败后，下一次尝试前需等待 delay(n) = min(2^(n-1) 秒, 上限)
//   - **超过次数后仍可继续尝试**，需求明确要求不永久锁死
//   - 成功输入一次密码后，计数清零
//
// 上限默认 60 秒。只统计「密码错误」，文件损坏、格式非法等不属于密码问题，
// 不应让用户干等。

#include <chrono>
#include <cstdint>

namespace secretkeeper::core {

class PasswordBackoff {
 public:
  static constexpr std::uint32_t kDefaultCapSeconds = 60;

  explicit PasswordBackoff(std::uint32_t cap_seconds = kDefaultCapSeconds)
      : cap_seconds_(cap_seconds) {}

  // 记录一次密码错误。返回此后需等待的秒数。
  std::uint32_t record_failure();

  // 密码正确，清零计数。
  void reset();

  std::uint32_t consecutive_failures() const noexcept { return failures_; }

  // 当前是否处于退避窗口内。
  bool is_waiting() const;

  // 退避窗口剩余秒数（向上取整）。不在窗口内返回 0。
  std::uint32_t remaining_seconds() const;

  std::uint32_t cap_seconds() const noexcept { return cap_seconds_; }

  // 纯函数形式，便于测试与 UI 预览：给定连续失败次数返回应等待秒数。
  static std::uint32_t delay_for(std::uint32_t consecutive_failures,
                                 std::uint32_t cap_seconds);

 private:
  using Clock = std::chrono::steady_clock;

  std::uint32_t cap_seconds_;
  std::uint32_t failures_ = 0;
  Clock::time_point window_start_{};
  bool waiting_ = false;
};

}  // namespace secretkeeper::core
