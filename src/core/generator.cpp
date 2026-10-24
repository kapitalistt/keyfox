#include "core/generator.hpp"

#include <sodium.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace kf {

namespace {

constexpr const char* kUpper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr const char* kLower = "abcdefghijklmnopqrstuvwxyz";
constexpr const char* kDigits = "0123456789";
constexpr const char* kSymbols = "!@#$%^&*()-_=+[]{};:,.<>?/~";
constexpr const char* kSimilar = "Il1O0o|";

// Phrase mode: each word is three consonant+vowel syllables (16*5 = 80 syllables),
// i.e. 80^3 = 512 000 equally likely words, about 18.97 bits each.
constexpr const char* kConsonants = "bdfghjklmnprstvz";
constexpr const char* kVowels = "aeiou";

std::uint32_t pick(std::size_t n) { return randombytes_uniform(static_cast<std::uint32_t>(n)); }

std::vector<std::string> classes(const GenOptions& o) {
    std::vector<std::string> out;
    auto add = [&](bool on, const char* set) {
        if (!on) return;
        std::string s = set;
        if (o.no_similar)
            s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return std::strchr(kSimilar, c) != nullptr; }),
                    s.end());
        out.push_back(std::move(s));
    };
    add(o.upper, kUpper);
    add(o.lower, kLower);
    add(o.digits, kDigits);
    add(o.symbols, kSymbols);
    if (out.empty()) out.emplace_back(kLower);
    return out;
}

GenResult generateChars(const GenOptions& o) {
    const auto cls = classes(o);
    std::string pool;
    for (const auto& c : cls) pool += c;

    std::size_t len = static_cast<std::size_t>(std::clamp(o.length, 4, 128));
    if (o.no_repeat) len = std::min(len, pool.size());

    GenResult r;
    for (int attempt = 0; attempt < 1000; ++attempt) {
        sodium_memzero(r.value.data(), r.value.size());
        r.value.clear();
        if (o.no_repeat) {
            std::string p = pool;  // partial Fisher-Yates
            for (std::size_t i = 0; i < len; ++i) {
                const std::size_t j = i + pick(p.size() - i);
                std::swap(p[i], p[j]);
                r.value += p[i];
            }
            sodium_memzero(p.data(), p.size());
        } else {
            for (std::size_t i = 0; i < len; ++i) r.value += pool[pick(pool.size())];
        }
        // Rejection sampling keeps the distribution uniform over passwords that
        // contain every selected character class.
        if (len < cls.size()) break;
        const bool all = std::all_of(cls.begin(), cls.end(), [&](const std::string& c) {
            return r.value.find_first_of(c) != std::string::npos;
        });
        if (all) break;
    }

    if (o.no_repeat) {
        for (std::size_t i = 0; i < len; ++i) r.entropy_bits += std::log2(static_cast<double>(pool.size() - i));
    } else {
        r.entropy_bits = static_cast<double>(len) * std::log2(static_cast<double>(pool.size()));
    }
    return r;
}

GenResult generatePhrase(const GenOptions& o) {
    const int words = std::clamp(o.words, 3, 12);
    const std::size_t nc = std::strlen(kConsonants), nv = std::strlen(kVowels);

    GenResult r;
    const std::uint32_t digit_word = pick(static_cast<std::size_t>(words));
    for (int w = 0; w < words; ++w) {
        if (w) r.value += '-';
        std::string word;
        for (int s = 0; s < 3; ++s) {
            word += kConsonants[pick(nc)];
            word += kVowels[pick(nv)];
        }
        word[0] = static_cast<char>(word[0] - 'a' + 'A');  // readable + satisfies "uppercase required"
        if (static_cast<std::uint32_t>(w) == digit_word) word += static_cast<char>('0' + pick(10));
        r.value += word;
    }
    r.entropy_bits = words * 3 * std::log2(static_cast<double>(nc * nv)) + std::log2(10.0 * words);
    return r;
}

}  // namespace

std::string generatorPool(const GenOptions& o) {
    if (o.mode == GenOptions::Mode::Phrase) return std::string(kConsonants) + kVowels;
    std::string pool;
    for (const auto& c : classes(o)) pool += c;
    return pool;
}

GenResult generate(const GenOptions& o) {
    return o.mode == GenOptions::Mode::Phrase ? generatePhrase(o) : generateChars(o);
}

}  // namespace kf
