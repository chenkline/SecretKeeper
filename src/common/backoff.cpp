#include "backoff.h"

#include <algorithm>

namespace secretkeeper::core {

std::uint32_t PasswordBackoff::delay_for(std::uint32_t consecutive_failures,
                                          std::uint32_t cap_seconds) {
  if (consecutive_failures == 0) return 0;
  // 2^(n-1) 在 n 较大时会溢出，先按位宽夹住再移位。
  constexpr std::uint32_t kMaxShift = 31;
  const std::uint32_t shift = std::min(consecutive_failures - 1, kMaxShift);
  const std::uint64_t secs = 1ULL << shift;
  if (secs >= cap_seconds) return cap_seconds;
  return static_cast<std::uint32_t>(secs);
}

std::uint32_t PasswordBackoff::record_failure() {
  ++failures_;
  const std::uint32_t secs = delay_for(failures_, cap_seconds_);
  window_start_ = Clock::now();
  waiting_ = secs > 0;
  return secs;
}

void PasswordBackoff::reset() {
  failures_ = 0;
  waiting_ = false;
  window_start_ = Clock::time_point{};
}

bool PasswordBackoff::is_waiting() const {
  if (!waiting_) return false;
  if (remaining_seconds() == 0) return false;
  return true;
}

std::uint32_t PasswordBackoff::remaining_seconds() const {
  if (!waiting_) return 0;
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - window_start_).count();
  const std::uint32_t total = delay_for(failures_, cap_seconds_);
  if (elapsed >= static_cast<std::int64_t>(total)) return 0;
  return static_cast<std::uint32_t>(total - static_cast<std::uint32_t>(elapsed));
}

}  // namespace secretkeeper::core
