#pragma once
// Colour palette. Everything is drawn on an explicit background colour so that
// fades can be computed by mixing towards it.

#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kf::theme {

struct Rgb {
    int r, g, b;
};

inline constexpr Rgb bg{15, 17, 21};
inline constexpr Rgb fg{216, 222, 233};
inline constexpr Rgb white{255, 255, 255};
inline constexpr Rgb dim{107, 114, 128};
inline constexpr Rgb dim2{75, 82, 99};
inline constexpr Rgb line{52, 58, 70};
inline constexpr Rgb track{34, 37, 44};
inline constexpr Rgb orange{255, 122, 26};
inline constexpr Rgb amber{255, 179, 71};
inline constexpr Rgb deep{255, 77, 0};
inline constexpr Rgb green{74, 222, 128};
inline constexpr Rgb red{248, 113, 113};
inline constexpr Rgb yellow{250, 204, 21};
inline constexpr Rgb cyan{103, 232, 249};
inline constexpr Rgb sel{43, 26, 12};
inline constexpr Rgb input_bg{24, 27, 33};
inline constexpr Rgb ink{17, 17, 17};

inline ftxui::Color c(Rgb x) {
    return ftxui::Color::RGB(static_cast<std::uint8_t>(std::clamp(x.r, 0, 255)),
                             static_cast<std::uint8_t>(std::clamp(x.g, 0, 255)),
                             static_cast<std::uint8_t>(std::clamp(x.b, 0, 255)));
}

inline Rgb mix(Rgb a, Rgb b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    auto m = [t](int x, int y) { return static_cast<int>(std::lround(x + (y - x) * t)); };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b)};
}

// Colour `target` faded in from the background by `alpha` (0 = invisible).
inline ftxui::Color fade(Rgb target, double alpha) { return c(mix(bg, target, alpha)); }

// red -> yellow -> green, t in [0, 1]
inline Rgb heat(double t) {
    return t < 0.5 ? mix(red, yellow, t * 2) : mix(yellow, green, (t - 0.5) * 2);
}

// The fox gradient: amber at the top, deep orange at the bottom.
inline Rgb foxGradient(double t) { return mix(amber, deep, t); }

}  // namespace kf::theme
