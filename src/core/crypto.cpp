#include "core/crypto.hpp"

#include <cstring>

namespace kf::crypto {

namespace {

constexpr unsigned char kMagic[4] = {'K', 'F', 'O', 'X'};
constexpr unsigned char kKdfArgon2id = 1;
constexpr unsigned char kAeadXChaCha = 1;

// Bounds accepted when reading a file. Anything outside is treated as corrupt,
// so a crafted file cannot make us allocate 64 GiB or spin for an hour.
constexpr std::uint32_t kMinOps = 1;
constexpr std::uint32_t kMaxOps = 32;
constexpr std::uint32_t kMinMemKib = 8 * 1024;          // 8 MiB
constexpr std::uint32_t kMaxMemKib = 4 * 1024 * 1024;   // 4 GiB

void putU16(unsigned char* p, std::uint16_t v) {
    p[0] = static_cast<unsigned char>(v);
    p[1] = static_cast<unsigned char>(v >> 8);
}
void putU32(unsigned char* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
}
std::uint16_t getU16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t getU32(const unsigned char* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    return v;
}

std::array<unsigned char, kHeaderBytes> encodeHeader(const Header& h) {
    std::array<unsigned char, kHeaderBytes> out{};
    std::memcpy(out.data(), kMagic, 4);
    putU16(out.data() + 4, kFormatVersion);
    out[6] = kKdfArgon2id;
    out[7] = kAeadXChaCha;
    putU32(out.data() + 8, h.kdf.opslimit);
    putU32(out.data() + 12, h.kdf.memlimit_kib);
    std::memcpy(out.data() + 16, h.salt.data(), kSaltBytes);
    std::memcpy(out.data() + 32, h.nonce.data(), kNonceBytes);
    return out;
}

}  // namespace

static_assert(crypto_pwhash_SALTBYTES == kSaltBytes);
static_assert(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES == kNonceBytes);
static_assert(crypto_aead_xchacha20poly1305_ietf_KEYBYTES == kKeyBytes);
static_assert(crypto_aead_xchacha20poly1305_ietf_ABYTES == kTagBytes);

void init() {
    if (sodium_init() < 0) throw std::runtime_error("libsodium initialisation failed");
}

KdfParams kdfPreset(std::string_view name) {
    if (name == "interactive") return {2, 64 * 1024};
    if (name == "strong") return {4, 1024 * 1024};
    return {3, 256 * 1024};
}

Salt randomSalt() {
    Salt s;
    randombytes_buf(s.data(), s.size());
    return s;
}

SecureBuffer deriveKey(std::string_view password, const Salt& salt, const KdfParams& kdf) {
    SecureBuffer key(kKeyBytes);
    const int rc = crypto_pwhash(key.data(), kKeyBytes, password.data(), password.size(), salt.data(),
                                 kdf.opslimit, static_cast<std::size_t>(kdf.memlimit_kib) * 1024u,
                                 crypto_pwhash_ALG_ARGON2ID13);
    if (rc != 0) throw std::runtime_error("Argon2id failed: not enough memory");
    key.protect();
    return key;
}

Header parseHeader(std::span<const unsigned char> file) {
    if (file.size() < kHeaderBytes + kTagBytes) throw FormatError("file is too short");
    if (std::memcmp(file.data(), kMagic, 4) != 0) throw FormatError("not a KeyFox vault");
    if (getU16(file.data() + 4) != kFormatVersion) throw FormatError("unsupported vault version");
    if (file[6] != kKdfArgon2id || file[7] != kAeadXChaCha) throw FormatError("unknown algorithm id");

    Header h;
    h.kdf.opslimit = getU32(file.data() + 8);
    h.kdf.memlimit_kib = getU32(file.data() + 12);
    if (h.kdf.opslimit < kMinOps || h.kdf.opslimit > kMaxOps || h.kdf.memlimit_kib < kMinMemKib ||
        h.kdf.memlimit_kib > kMaxMemKib)
        throw FormatError("KDF parameters out of range");
    std::memcpy(h.salt.data(), file.data() + 16, kSaltBytes);
    std::memcpy(h.nonce.data(), file.data() + 32, kNonceBytes);
    return h;
}

std::vector<unsigned char> seal(const SecureBytes& plaintext, SecureBuffer& key, Header& header) {
    randombytes_buf(header.nonce.data(), header.nonce.size());
    const auto hdr = encodeHeader(header);

    std::vector<unsigned char> out(kHeaderBytes + plaintext.size() + kTagBytes);
    std::memcpy(out.data(), hdr.data(), kHeaderBytes);

    unsigned long long clen = 0;
    ReadAccess access(key);
    crypto_aead_xchacha20poly1305_ietf_encrypt(out.data() + kHeaderBytes, &clen, plaintext.data(),
                                               plaintext.size(), hdr.data(), hdr.size(), nullptr,
                                               header.nonce.data(), key.data());
    out.resize(kHeaderBytes + static_cast<std::size_t>(clen));
    return out;
}

SecureBytes open(std::span<const unsigned char> file, SecureBuffer& key) {
    const Header h = parseHeader(file);
    const auto ct = file.subspan(kHeaderBytes);

    SecureBytes plain(ct.size() - kTagBytes);
    unsigned long long mlen = 0;
    ReadAccess access(key);
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(plain.data(), &mlen, nullptr, ct.data(), ct.size(),
                                                   file.data(), kHeaderBytes, h.nonce.data(),
                                                   key.data()) != 0)
        throw AuthError();
    plain.resize(static_cast<std::size_t>(mlen));
    return plain;
}

}  // namespace kf::crypto
