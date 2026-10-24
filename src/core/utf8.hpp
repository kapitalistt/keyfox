#pragma once
// Minimal UTF-8 helpers (glyph splitting, case folding for Latin + Cyrillic).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kf::utf8 {

inline std::size_t seqLen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

// Splits a UTF-8 string into code points (each returned as its own UTF-8 string).
inline std::vector<std::string> glyphs(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t n = seqLen(static_cast<unsigned char>(s[i]));
        if (i + n > s.size()) n = 1;
        out.emplace_back(s.substr(i, n));
        i += n;
    }
    return out;
}

inline std::size_t length(std::string_view s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); i += seqLen(static_cast<unsigned char>(s[i]))) ++n;
    return n;
}

inline char32_t decode(std::string_view g) {
    auto b = [&](std::size_t i) { return static_cast<char32_t>(static_cast<unsigned char>(g[i])); };
    switch (g.size()) {
        case 1: return b(0);
        case 2: return ((b(0) & 0x1F) << 6) | (b(1) & 0x3F);
        case 3: return ((b(0) & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F);
        case 4: return ((b(0) & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);
        default: return 0xFFFD;
    }
}

inline void encode(char32_t c, std::string& out) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

inline char32_t lowerCp(char32_t c) {
    if (c >= U'A' && c <= U'Z') return static_cast<char32_t>(c + 32);
    if (c >= 0x0410 && c <= 0x042F) return static_cast<char32_t>(c + 0x20);  // А..Я
    if (c == 0x0401) return 0x0451;                   // Ё
    return c;
}

inline std::string lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const auto& g : glyphs(s)) encode(lowerCp(decode(g)), out);
    return out;
}

inline bool containsCi(std::string_view haystack, std::string_view lowered_needle) {
    if (lowered_needle.empty()) return true;
    return lower(haystack).find(lowered_needle) != std::string::npos;
}

}  // namespace kf::utf8
