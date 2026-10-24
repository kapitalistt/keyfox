#pragma once
// Rough password strength estimate for passwords the user typed in.
// (Generated passwords report their exact entropy instead.)

#include <string_view>

namespace kf {

struct Strength {
    double bits = 0;
    int level = 0;  // 0 very weak, 1 weak, 2 fair, 3 strong, 4 excellent
};

Strength estimateStrength(std::string_view password);
int strengthLevel(double bits);

// Expected seconds to brute-force at `guesses_per_second` (half the key space).
double crackSeconds(double bits, double guesses_per_second = 1e12);

}  // namespace kf
