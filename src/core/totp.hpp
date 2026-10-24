#pragma once
// TOTP (RFC 6238) on top of HOTP (RFC 4226). HMAC comes from Windows CNG (BCrypt).

#include "core/secure.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kf {

struct TotpConfig {
    enum class Algo { Sha1, Sha256, Sha512 };
    SecureBytes secret;
    Algo algo = Algo::Sha1;
    int digits = 6;
    int period = 30;
};

// Accepts a bare base32 secret ("JBSW Y3DP ...") or an otpauth://totp/... URI.
std::optional<TotpConfig> parseTotp(std::string_view input);

std::optional<SecureBytes> base32Decode(std::string_view in);

std::string hotp(const TotpConfig& cfg, std::uint64_t counter);
std::string totpCode(const TotpConfig& cfg, std::int64_t unix_time);
int totpSecondsLeft(const TotpConfig& cfg, std::int64_t unix_time);

}  // namespace kf
