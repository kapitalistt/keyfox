#pragma once
// Vault file cryptography.
//
// File layout (all integers little-endian):
//
//   offset size  field
//   0      4     magic "KFOX"
//   4      2     format version (1)
//   6      1     KDF id      (1 = Argon2id v1.3)
//   7      1     AEAD id     (1 = XChaCha20-Poly1305-IETF)
//   8      4     Argon2 opslimit (passes)
//   12     4     Argon2 memlimit in KiB
//   16     16    salt
//   32     24    nonce (fresh random value on every save)
//   56     8     reserved (zero)
//   64     ...   ciphertext || 16-byte Poly1305 tag
//
// The whole 64-byte header is authenticated as associated data, so changing
// KDF parameters, salt or nonce makes decryption fail.

#include "core/secure.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kf::crypto {

inline constexpr std::size_t kKeyBytes = 32;
inline constexpr std::size_t kSaltBytes = 16;
inline constexpr std::size_t kNonceBytes = 24;
inline constexpr std::size_t kTagBytes = 16;
inline constexpr std::size_t kHeaderBytes = 64;
inline constexpr std::uint16_t kFormatVersion = 1;

using Salt = std::array<unsigned char, kSaltBytes>;
using Nonce = std::array<unsigned char, kNonceBytes>;

struct KdfParams {
    std::uint32_t opslimit = 3;
    std::uint32_t memlimit_kib = 256 * 1024;  // 256 MiB
};

// "interactive" (64 MiB, t=2), "moderate" (256 MiB, t=3, default), "strong" (1 GiB, t=4).
KdfParams kdfPreset(std::string_view name);

struct Header {
    KdfParams kdf;
    Salt salt{};
    Nonce nonce{};
};

struct AuthError : std::runtime_error {
    AuthError() : std::runtime_error("wrong password or the vault was modified") {}
};
struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Must be called once before anything else (wraps sodium_init).
void init();

Salt randomSalt();

// Argon2id(password, salt) -> 32-byte key in guarded memory (left protected).
SecureBuffer deriveKey(std::string_view password, const Salt& salt, const KdfParams& kdf);

// Parses and validates the header (sane KDF bounds protect against DoS files).
Header parseHeader(std::span<const unsigned char> file);

// Encrypts `plaintext`, generating a new nonce. Returns the complete file image.
std::vector<unsigned char> seal(const SecureBytes& plaintext, SecureBuffer& key, Header& header);

// Authenticates and decrypts a complete file image. Throws AuthError / FormatError.
SecureBytes open(std::span<const unsigned char> file, SecureBuffer& key);

}  // namespace kf::crypto
