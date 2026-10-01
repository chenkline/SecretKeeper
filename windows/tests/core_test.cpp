// SecretKeeper - core layer self-check: code point counting, backoff, KEK cache,
// error message text.
//
// Test output must stay pure ASCII: the CI console code page is not UTF-8 and
// non-ASCII printf output fails the whole job.

#include <cstdio>
#include <string>

#include "../src/core/backoff.h"
#include "../src/core/error.h"
#include "../src/core/kek_cache.h"
#include "../src/core/text.h"

namespace core = secretkeeper::core;
namespace text = secretkeeper::text;
namespace crypto = secretkeeper::crypto;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL  %s\n", what.c_str());
  }
}

std::string from_hex(const std::string& s) {
  std::string out;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
    out.push_back(static_cast<char>((nib(s[i]) << 4) | nib(s[i + 1])));
  }
  return out;
}

void test_code_points() {
  std::printf("Unicode code point counting\n");

  // ASCII
  check(text::count_code_points("abc") == 3, "ascii counts bytes");
  check(text::count_code_points("") == 0, "empty string is 0 code points");
  check(text::count_code_points(std::string(150, 'a')) == 150, "150 ascii is 150");
  check(text::count_code_points(std::string(151, 'a')) == 151, "151 ascii is 151");

  // U+00E9 e-acute: 2 UTF-8 bytes, 1 code point, 1 UTF-16 unit.
  check(text::count_code_points(from_hex("c3a9")) == 1, "2-byte sequence is 1 code point");

  // U+4E2D CJK: 3 UTF-8 bytes, 1 code point.
  check(text::count_code_points(from_hex("e4b8ad")) == 1, "3-byte sequence is 1 code point");

  // U+1F600 emoji: 4 UTF-8 bytes, 1 code point, 2 UTF-16 units.
  // This is the case that breaks naive UTF-16 or byte-length counting.
  const std::string emoji = from_hex("f09f9880");
  check(text::count_code_points(emoji) == 1, "emoji (non-BMP) is 1 code point");
  check(emoji.size() == 4, "emoji occupies 4 UTF-8 bytes");

  // Mixed string: "a" + emoji + CJK = 3 code points, 1+4+3 = 8 bytes.
  const std::string mixed = std::string("a") + emoji + from_hex("e4b8ad");
  check(text::count_code_points(mixed) == 3, "mixed string counts 3 code points");
  check(mixed.size() == 8, "mixed string is 8 UTF-8 bytes");

  std::string cjk150;
  for (int i = 0; i < 150; ++i) cjk150 += from_hex("e4b8ad");
  check(text::count_code_points(cjk150) == 150, "150 CJK chars is 150 code points");
  check(cjk150.size() == 450, "150 CJK chars is 450 UTF-8 bytes");

  std::string emoji150;
  for (int i = 0; i < 150; ++i) emoji150 += emoji;
  check(text::count_code_points(emoji150) == 150, "150 emoji is 150 code points");
  check(emoji150.size() == 600, "150 emoji is 600 UTF-8 bytes");

  // Limit constant must match the requirement.
  check(text::kMaxSecretLength == 150, "limit constant is 150");

  // UTF-8 validation
  check(text::is_valid_utf8("hello"), "ascii is valid utf8");
  check(text::is_valid_utf8(emoji), "emoji is valid utf8");
  check(text::is_valid_utf8(from_hex("c3a9")), "2-byte sequence is valid utf8");
  check(!text::is_valid_utf8(from_hex("c3")), "truncated sequence is invalid");
  check(!text::is_valid_utf8(from_hex("80")), "lone continuation byte is invalid");
  check(!text::is_valid_utf8(from_hex("eda080")), "surrogate U+D800 is invalid");
  check(!text::is_valid_utf8(from_hex("c080")), "overlong encoding is invalid");
}

void test_backoff() {
  std::printf("Password exponential backoff\n");

  // delay(n) = min(2^(n-1), cap)
  check(core::PasswordBackoff::delay_for(0, 60) == 0, "0 failures needs no wait");
  check(core::PasswordBackoff::delay_for(1, 60) == 1, "1 failure -> 1s");
  check(core::PasswordBackoff::delay_for(2, 60) == 2, "2 failures -> 2s");
  check(core::PasswordBackoff::delay_for(3, 60) == 4, "3 failures -> 4s");
  check(core::PasswordBackoff::delay_for(4, 60) == 8, "4 failures -> 8s");
  check(core::PasswordBackoff::delay_for(7, 60) == 60, "7 failures -> capped at 60s");
  check(core::PasswordBackoff::delay_for(100, 60) == 60, "100 failures -> still 60s");
  // No permanent lockout: the delay stops growing but attempts remain possible.
  check(core::PasswordBackoff::delay_for(1000, 60) == 60, "1000 failures still 60s, not locked out");

  // Overflow safety: shift is clamped before it can wrap around.
  check(core::PasswordBackoff::delay_for(31, 60) == 60, "shift clamped, no overflow");
  check(core::PasswordBackoff::delay_for(63, 60) == 60, "shift clamped at 63");

  core::PasswordBackoff b(60);
  check(!b.is_waiting(), "fresh backoff is not waiting");
  check(b.record_failure() == 1, "first failure asks for 1s");
  check(b.consecutive_failures() == 1, "counter is 1");
  check(b.record_failure() == 2, "second failure asks for 2s");
  check(b.consecutive_failures() == 2, "counter is 2");
  b.reset();
  check(b.consecutive_failures() == 0, "reset clears the counter");
  check(!b.is_waiting(), "reset clears the wait window");

  // Smaller cap for testability.
  core::PasswordBackoff small(3);
  check(small.record_failure() == 1, "cap 3, first failure 1s");
  check(small.record_failure() == 2, "cap 3, second failure 2s");
  check(small.record_failure() == 3, "cap 3, third failure capped at 3s");
  check(small.record_failure() == 3, "cap 3, fourth failure still 3s (not locked out)");
}

void test_kek_cache() {
  std::printf("KEK cache (3 slots, rotating)\n");

  core::KekCache cache;
  check(cache.slot_count() == 3, "exactly 3 slots as required");
  check(!cache.has_value(), "empty cache has no value");
  check(!cache.peek().has_value(), "empty cache cannot be peeked");

  crypto::Kek kek{};
  for (std::size_t i = 0; i < sizeof(kek); ++i) {
    kek[i] = static_cast<std::uint8_t>(i * 7 + 1);
  }

  check(cache.load(kek), "load succeeds");
  check(cache.has_value(), "cache reports loaded");

  // Round-trip must return the exact same KEK.
  const auto peeked = cache.peek();
  check(peeked.has_value(), "peek returns a value after load");
  if (peeked) check(*peeked == kek, "KEK round-trips through the cache unchanged");

  const auto slot0 = cache.active_slot();
  check(slot0.has_value(), "active slot is reported");

  // Rotation must migrate to a DIFFERENT slot (decoy semantics).
  bool ever_moved = false;
  bool ever_saw_decoy = false;
  for (int i = 0; i < 30; ++i) {
    const auto before = cache.active_slot();
    const auto after = cache.rotate();
    check(after.has_value(), "rotate returns a slot");
    if (before && after) {
      if (*before != *after) ever_moved = true;
      // At least two of three slots occupied means a decoy is left behind.
      std::size_t occupied = 0;
      for (std::size_t s = 0; s < cache.slot_count(); ++s) {
        if (cache.slot_occupied(s)) ++occupied;
      }
      if (occupied >= 2) ever_saw_decoy = true;
    }
    const auto again = cache.peek();
    check(again.has_value(), "peek works after rotate");
    if (again) check(*again == kek, "KEK survives rotation intact");
  }
  check(ever_moved, "rotation migrates between slots");
  check(ever_saw_decoy, "old slots are left occupied as decoys");

  // Lock clears everything.
  cache.clear();
  check(!cache.has_value(), "clear resets loaded flag");
  check(!cache.peek().has_value(), "clear wipes the KEK");
  for (std::size_t s = 0; s < cache.slot_count(); ++s) {
    check(!cache.slot_occupied(s), "clear wipes every slot");
  }

  // Reload after clear must work and yield a usable cache.
  check(cache.load(kek), "reload after clear succeeds");
  const auto reloaded = cache.peek();
  check(reloaded.has_value() && *reloaded == kek, "reloaded KEK is intact");
  cache.clear();
}

void test_error_messages() {
  std::printf("Error messages and classification\n");

  // Requirement: every password failure shows the same wording.
  check(core::message(core::Error::kPasswordWrong) == "\xe4\xb8\xbb\xe5\xaf\x86\xe9\x92\xa5\xe5\xaf\x86\xe7\xa0\x81\xe9\x94\x99\xe8\xaf\xaf",
        "password error wording matches requirement");

  // Only password errors feed the backoff counter.
  check(core::is_password_error(core::Error::kPasswordWrong), "password error counts toward backoff");
  check(!core::is_password_error(core::Error::kImportBadFormat), "bad format does not count");
  check(!core::is_password_error(core::Error::kMasterKeyNotFound), "not-found does not count");
  check(!core::is_password_error(core::Error::kImportMasterKeyMissing),
        "missing master key does not count");
  check(!core::is_password_error(core::Error::kIoError), "io error does not count");

  // Every non-ok error must have a non-empty message; the UI shows it verbatim.
  const core::Error all[] = {
      core::Error::kPasswordWrong,     core::Error::kRateLimited,
      core::Error::kMasterKeyQuotaExceeded, core::Error::kMasterKeyIsDefault,
      core::Error::kMasterKeyNotFound, core::Error::kCannotDeleteDefault,
      core::Error::kSecretQuotaExceeded, core::Error::kSecretTooLong,
      core::Error::kSecretNotFound,    core::Error::kEmptySecret,
      core::Error::kImportBadFormat,   core::Error::kImportVersionTooHigh,
      core::Error::kImportMasterKeyMissing, core::Error::kImportDecryptFailed,
      core::Error::kImportSaveFailed,  core::Error::kImportConflict,
      core::Error::kIoError,           core::Error::kCryptoUnsupported,
      core::Error::kInternal,
  };
  for (core::Error e : all) {
    check(!core::message(e).empty(), "every error carries a message");
  }
  check(core::message(core::Error::kOk).empty(), "ok has no message");

  // Wording must be distinct where the requirement distinguishes outcomes.
  check(core::message(core::Error::kImportBadFormat) != core::message(core::Error::kImportVersionTooHigh),
        "bad format and version-too-high are worded differently");
  check(core::message(core::Error::kImportMasterKeyMissing) != core::message(core::Error::kImportDecryptFailed),
        "missing master key and decrypt failure are worded differently");
}

}  // namespace

int main() {
  std::printf("Core layer self-check\n");
  test_code_points();
  test_backoff();
  test_kek_cache();
  test_error_messages();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
