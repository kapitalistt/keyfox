#include "core/totp.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>

namespace kf {

namespace {

SecureBytes hmac(TotpConfig::Algo algo, const SecureBytes& key, const unsigned char* msg, ULONG msg_len) {
    LPCWSTR id = BCRYPT_SHA1_ALGORITHM;
    ULONG out_len = 20;
    if (algo == TotpConfig::Algo::Sha256) { id = BCRYPT_SHA256_ALGORITHM; out_len = 32; }
    if (algo == TotpConfig::Algo::Sha512) { id = BCRYPT_SHA512_ALGORITHM; out_len = 64; }

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    SecureBytes out(out_len);
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg, id, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (BCRYPT_SUCCESS(st))
        st = BCryptCreateHash(alg, &hash, nullptr, 0, const_cast<PUCHAR>(key.data()),
                              static_cast<ULONG>(key.size()), 0);
    if (BCRYPT_SUCCESS(st)) st = BCryptHashData(hash, const_cast<PUCHAR>(msg), msg_len, 0);
    if (BCRYPT_SUCCESS(st)) st = BCryptFinishHash(hash, out.data(), out_len, 0);
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (!BCRYPT_SUCCESS(st)) throw std::runtime_error("BCrypt HMAC failed");
    return out;
}

std::string percentDecode(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::optional<SecureBytes> base32Decode(std::string_view in) {
    SecureBytes out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char ch : in) {
        if (ch == ' ' || ch == '-' || ch == '=') continue;
        const auto c = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= '2' && c <= '7') v = c - '2' + 26;
        else return std::nullopt;
        buffer = (buffer << 5) | static_cast<std::uint32_t>(v);
        bits += 5;
        if (bits >= 8) {
            out.push_back(static_cast<unsigned char>(buffer >> (bits - 8)));
            bits -= 8;
        }
    }
    if (out.empty()) return std::nullopt;
    return out;
}

std::optional<TotpConfig> parseTotp(std::string_view input) {
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) input.remove_prefix(1);
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) input.remove_suffix(1);
    if (input.empty()) return std::nullopt;

    TotpConfig cfg;
    constexpr std::string_view kScheme = "otpauth://";
    std::string head(input.substr(0, std::min(input.size(), kScheme.size())));
    std::transform(head.begin(), head.end(), head.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (head != kScheme) {
        auto secret = base32Decode(input);
        if (!secret) return std::nullopt;
        cfg.secret = std::move(*secret);
        return cfg;
    }

    std::string rest(input.substr(kScheme.size()));
    std::string type = rest.substr(0, rest.find('/'));
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (type != "totp") return std::nullopt;

    const auto q = rest.find('?');
    if (q == std::string::npos) return std::nullopt;
    std::string_view query(rest);
    query.remove_prefix(q + 1);

    bool have_secret = false;
    while (!query.empty()) {
        const auto amp = query.find('&');
        const std::string_view pair = query.substr(0, amp);
        query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
        const auto eq = pair.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string key = upper(std::string(pair.substr(0, eq)));
        const std::string value = percentDecode(pair.substr(eq + 1));
        try {
            if (key == "SECRET") {
                auto secret = base32Decode(value);
                if (!secret) return std::nullopt;
                cfg.secret = std::move(*secret);
                have_secret = true;
            } else if (key == "ALGORITHM") {
                const std::string a = upper(value);
                if (a == "SHA1") cfg.algo = TotpConfig::Algo::Sha1;
                else if (a == "SHA256") cfg.algo = TotpConfig::Algo::Sha256;
                else if (a == "SHA512") cfg.algo = TotpConfig::Algo::Sha512;
                else return std::nullopt;
            } else if (key == "DIGITS") {
                cfg.digits = std::stoi(value);
            } else if (key == "PERIOD") {
                cfg.period = std::stoi(value);
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    if (!have_secret || cfg.digits < 6 || cfg.digits > 9 || cfg.period < 1 || cfg.period > 300) return std::nullopt;
    return cfg;
}

std::string hotp(const TotpConfig& cfg, std::uint64_t counter) {
    std::array<unsigned char, 8> msg{};
    for (int i = 7; i >= 0; --i) {
        msg[static_cast<std::size_t>(i)] = static_cast<unsigned char>(counter & 0xFF);
        counter >>= 8;
    }
    const SecureBytes h = hmac(cfg.algo, cfg.secret, msg.data(), static_cast<ULONG>(msg.size()));
    const std::size_t off = h.back() & 0x0F;
    const std::uint32_t bin = (static_cast<std::uint32_t>(h[off] & 0x7F) << 24) |
                              (static_cast<std::uint32_t>(h[off + 1]) << 16) |
                              (static_cast<std::uint32_t>(h[off + 2]) << 8) | static_cast<std::uint32_t>(h[off + 3]);
    std::uint32_t mod = 1;
    for (int i = 0; i < cfg.digits; ++i) mod *= 10;
    std::string code = std::to_string(bin % mod);
    if (code.size() < static_cast<std::size_t>(cfg.digits))
        code.insert(0, static_cast<std::size_t>(cfg.digits) - code.size(), '0');
    return code;
}

std::string totpCode(const TotpConfig& cfg, std::int64_t unix_time) {
    return hotp(cfg, static_cast<std::uint64_t>(unix_time / cfg.period));
}

int totpSecondsLeft(const TotpConfig& cfg, std::int64_t unix_time) {
    return cfg.period - static_cast<int>(unix_time % cfg.period);
}

}  // namespace kf
