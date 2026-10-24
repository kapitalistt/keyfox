#pragma once
// Password / passphrase generator. All randomness comes from libsodium's CSPRNG
// (randombytes_uniform, which is free of modulo bias).

#include <string>

namespace kf {

struct GenOptions {
    enum class Mode { Chars, Phrase };
    Mode mode = Mode::Chars;
    int length = 20;   // characters (Chars mode), 4..128
    int words = 5;     // words (Phrase mode), 3..12
    bool upper = true;
    bool lower = true;
    bool digits = true;
    bool symbols = true;
    bool no_similar = false;  // drop I l 1 O 0 o |
    bool no_repeat = false;   // every character at most once
};

struct GenResult {
    std::string value;
    double entropy_bits = 0;
};

GenResult generate(const GenOptions& o);

// Characters the generator may pick from with these options (used by the UI
// for the slot-machine animation).
std::string generatorPool(const GenOptions& o);

}  // namespace kf
