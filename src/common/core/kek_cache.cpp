#include "core/kek_cache.h"

#include <cstring>

namespace secretkeeper::core {

KekCache::~KekCache() { clear(); }

bool KekCache::load(const crypto::Kek& kek) {
  clear();
  crypto::random_bytes(std::span<std::uint8_t>(session_key_));
  session_key_set_ = true;
  // 载入的 KEK 明文另存一份：轮换只是「换槽位 + 换 nonce 重新加密」，
  // 若每次都先解密再加密就多了一轮无用功，也多一次明文暴露窗口。
  plain_ = kek;
  return rotate_into(0, kek).has_value();
}

std::optional<std::size_t> KekCache::rotate() {
  if (!loaded_ || !session_key_set_) return std::nullopt;
  // 随机迁移到**另一个**槽位。原地重写会让诱饵失效——攻击者只需比较
  // 两个时刻的内存，就能定位到正在使用的那一个。
  std::size_t target = active_;
  if (kKekSlotCount > 1) {
    const std::size_t span = static_cast<std::size_t>(kKekSlotCount) - 1;
    target = active_ + 1 + (crypto::generate_id()[0] % span);
    target %= kKekSlotCount;
    if (target == active_) target = (active_ + 1) % kKekSlotCount;
  }
  return rotate_into(target, plain_);
}

std::optional<std::size_t> KekCache::rotate_into(std::size_t slot_index, const crypto::Kek& kek) {
  if (slot_index >= kKekSlotCount || !session_key_set_) return std::nullopt;
  Slot& slot = slots_[slot_index];
  crypto::random_bytes(std::span<std::uint8_t>(slot.nonce));
  slot.cipher.assign(crypto::kKekLength + crypto::kTagLength, 0);
  crypto::aes_gcm_encrypt(session_key_.data(), session_key_.size(), slot.nonce,
                          std::span<const std::uint8_t>(), std::span<const std::uint8_t>(kek),
                          slot.cipher.data(), slot.tag.data());
  slot.occupied = true;
  active_ = slot_index;
  loaded_ = true;
  return slot_index;
}

void KekCache::clear() {
  for (Slot& slot : slots_) {
    if (!slot.cipher.empty()) {
      std::memset(slot.cipher.data(), 0, slot.cipher.size());
    }
    slot.cipher.clear();
    std::memset(slot.nonce.data(), 0, slot.nonce.size());
    std::memset(slot.tag.data(), 0, slot.tag.size());
    slot.occupied = false;
  }
  std::memset(session_key_.data(), 0, session_key_.size());
  std::memset(plain_.data(), 0, plain_.size());
  session_key_set_ = false;
  loaded_ = false;
  active_ = 0;
}

std::optional<crypto::Kek> KekCache::peek() const {
  if (!loaded_ || !session_key_set_) return std::nullopt;
  const Slot& slot = slots_[active_];
  if (!slot.occupied) return std::nullopt;
  crypto::Kek out{};
  const crypto::CryptoError rc =
      crypto::aes_gcm_decrypt(session_key_.data(), session_key_.size(), slot.nonce,
                              std::span<const std::uint8_t>(),
                              std::span<const std::uint8_t>(slot.cipher.data(),
                                                            crypto::kKekLength),
                              slot.tag, out.data());
  if (rc != crypto::CryptoError::kOk) return std::nullopt;
  return out;
}

std::optional<std::size_t> KekCache::active_slot() const noexcept {
  if (!loaded_) return std::nullopt;
  return active_;
}

bool KekCache::slot_occupied(std::size_t index) const noexcept {
  return index < slots_.size() && slots_[index].occupied;
}

}  // namespace secretkeeper::core
