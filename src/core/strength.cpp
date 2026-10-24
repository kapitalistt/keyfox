#include "core/strength.hpp"

#include "core/utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace kf {

namespace {

constexpr const char* kCommon[] = {
    "password", "passw0rd", "qwerty", "123456", "111111", "123123", "000000", "654321", "admin",
    "letmein", "welcome", "iloveyou", "monkey", "dragon", "football", "baseball", "master", "abc123",
    "1q2w3e", "zxcvbn", "asdfgh", "qazwsx", "sunshine", "princess", "secret", "пароль", "йцукен",
    "любовь", "привет"};

}  // namespace

int strengthLevel(double bits) {
    if (bits < 28) return 0;
    if (bits < 40) return 1;
    if (bits < 60) return 2;
    if (bits < 80) return 3;
    return 4;
}

Strength estimateStrength(std::string_view pw) {
    Strength s;
    if (pw.empty()) return s;

    const auto g = utf8::glyphs(pw);
    bool lo = false, up = false, di = false, sy = false, other = false;
    for (const auto& c : g) {
        if (c.size() > 1) { other = true; continue; }
        const auto ch = static_cast<unsigned char>(c[0]);
        if (std::islower(ch)) lo = true;
        else if (std::isupper(ch)) up = true;
        else if (std::isdigit(ch)) di = true;
        else sy = true;
    }
    const double pool = (lo ? 26 : 0) + (up ? 26 : 0) + (di ? 10 : 0) + (sy ? 33 : 0) + (other ? 66 : 0);
    const double per = std::log2(std::max(pool, 2.0));

    // Repeats ("aaa") and runs ("abc", "321") add almost nothing.
    int weak = 0;
    for (std::size_t i = 1; i < g.size(); ++i) {
        if (g[i] == g[i - 1]) { ++weak; continue; }
        if (g[i].size() == 1 && g[i - 1].size() == 1 && std::abs(g[i][0] - g[i - 1][0]) == 1) ++weak;
    }
    s.bits = (static_cast<double>(g.size()) - 0.75 * weak) * per;

    const std::string low = utf8::lower(pw);
    for (const char* c : kCommon) {
        if (low.find(c) != std::string::npos) {
            s.bits = std::min(s.bits, low == c ? 4.0 : 22.0);
            break;
        }
    }
    s.bits = std::max(0.0, s.bits);
    s.level = strengthLevel(s.bits);
    return s;
}

double crackSeconds(double bits, double guesses_per_second) {
    return std::pow(2.0, std::max(0.0, bits - 1)) / guesses_per_second;
}

}  // namespace kf
