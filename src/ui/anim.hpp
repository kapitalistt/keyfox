#pragma once
// Animation helpers: time, easing, deterministic noise, text effects.

#include "core/utf8.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace kf::anim {

using Clock = std::chrono::steady_clock;

inline double since(Clock::time_point t) {
    return std::chrono::duration<double>(Clock::now() - t).count();
}

inline double clamp01(double t) { return std::clamp(t, 0.0, 1.0); }

inline double easeOutCubic(double t) {
    t = clamp01(t);
    return 1 - std::pow(1 - t, 3);
}

inline double easeInOut(double t) {
    t = clamp01(t);
    return 0.5 - 0.5 * std::cos(t * 3.14159265358979);
}

inline double easeOutBack(double t) {
    t = clamp01(t);
    constexpr double c1 = 1.70158, c3 = c1 + 1;
    return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2);
}

// Progress of a step that starts at `delay` and lasts `duration` seconds.
inline double phase(double t, double delay, double duration) { return clamp01((t - delay) / duration); }

inline const char* spinner(double t) {
    static const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    return frames[static_cast<int>(t * 12) % 10];
}

inline std::uint32_t hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Deterministic pseudo-random value in [0, 1] (purely visual; never used for secrets).
inline double noise(std::uint32_t a, std::uint32_t b) {
    return (hash(a * 73856093U ^ b * 19349663U) & 0xFFFF) / 65535.0;
}

inline constexpr std::string_view kGlitch = "!@#$%&*+=?<>/\\|01abcdefXYZ";

inline char glitchChar(std::uint32_t a, std::uint32_t b) {
    return kGlitch[hash(a * 2654435761U + b) % kGlitch.size()];
}

// "Decrypt" effect: glyphs left of `progress` are final, the rest flicker.
inline std::string scramble(std::string_view target, double progress, std::uint32_t frame) {
    const auto g = utf8::glyphs(target);
    const auto shown = static_cast<std::size_t>(clamp01(progress) * static_cast<double>(g.size()));
    std::string out;
    for (std::size_t i = 0; i < g.size(); ++i) {
        if (i < shown || g[i] == " ") out += g[i];
        else out += glitchChar(static_cast<std::uint32_t>(i), frame);
    }
    return out;
}

// Typewriter effect.
inline std::string typed(std::string_view target, double progress) {
    const auto g = utf8::glyphs(target);
    const auto shown = static_cast<std::size_t>(clamp01(progress) * static_cast<double>(g.size()));
    std::string out;
    for (std::size_t i = 0; i < shown; ++i) out += g[i];
    return out;
}

inline std::uint32_t frameNo(double t, double fps = 30) { return static_cast<std::uint32_t>(t * fps); }

}  // namespace kf::anim
