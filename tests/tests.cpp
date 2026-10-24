// Minimal self-contained tests for the non-UI core (run with ctest).

#include "core/crypto.hpp"
#include "core/generator.hpp"
#include "core/strength.hpp"
#include "core/totp.hpp"
#include "core/utf8.hpp"
#include "core/vault.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failed = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failed;                                                          \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                        \
    } while (0)

template <class Ex>
static bool throws(const std::function<void()>& f) {
    try {
        f();
    } catch (const Ex&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

static kf::TotpConfig rawSecret(const std::string& ascii, kf::TotpConfig::Algo algo) {
    kf::TotpConfig c;
    c.secret.assign(ascii.begin(), ascii.end());
    c.algo = algo;
    c.digits = 8;
    return c;
}

static void testTotp() {
    std::puts("totp: RFC 6238 test vectors");
    using A = kf::TotpConfig::Algo;
    const auto s1 = rawSecret("12345678901234567890", A::Sha1);
    const auto s256 = rawSecret("12345678901234567890123456789012", A::Sha256);
    const auto s512 = rawSecret("1234567890123456789012345678901234567890123456789012345678901234", A::Sha512);
    CHECK(kf::totpCode(s1, 59) == "94287082");
    CHECK(kf::totpCode(s1, 1111111109) == "07081804");
    CHECK(kf::totpCode(s1, 2000000000) == "69279037");
    CHECK(kf::totpCode(s256, 59) == "46119246");
    CHECK(kf::totpCode(s256, 1111111109) == "68084774");
    CHECK(kf::totpCode(s512, 59) == "90693936");
    CHECK(kf::totpCode(s512, 1111111109) == "25091201");

    std::puts("totp: base32 + otpauth parsing");
    const auto b = kf::base32Decode("jbsw y3dp ehpk 3pxp");
    CHECK(b && std::string(b->begin(), b->end()) == "Hello!\xDE\xAD\xBE\xEF");
    CHECK(!kf::base32Decode("not base32 !!"));
    const auto uri = kf::parseTotp("otpauth://totp/GitHub:alex?secret=JBSWY3DPEHPK3PXP&issuer=GitHub&algorithm=SHA256&digits=8&period=60");
    CHECK(uri && uri->algo == A::Sha256 && uri->digits == 8 && uri->period == 60);
    CHECK(!kf::parseTotp("otpauth://hotp/x?secret=JBSWY3DPEHPK3PXP"));
    CHECK(kf::totpSecondsLeft(s1, 59) == 1);
}

static void testGenerator() {
    std::puts("generator: lengths, classes, uniqueness");
    kf::GenOptions o;
    o.length = 32;
    for (int i = 0; i < 200; ++i) {
        const auto r = kf::generate(o);
        CHECK(r.value.size() == 32);
        CHECK(r.value.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos);
        CHECK(r.value.find_first_of("0123456789") != std::string::npos);
    }
    o.no_repeat = true;
    o.no_similar = true;
    const auto u = kf::generate(o);
    CHECK(std::set<char>(u.value.begin(), u.value.end()).size() == u.value.size());
    CHECK(u.value.find_first_of("Il1O0o|") == std::string::npos);

    std::set<std::string> seen;
    for (int i = 0; i < 1000; ++i) seen.insert(kf::generate(o).value);
    CHECK(seen.size() == 1000);

    kf::GenOptions p;
    p.mode = kf::GenOptions::Mode::Phrase;
    p.words = 5;
    const auto ph = kf::generate(p);
    CHECK(std::count(ph.value.begin(), ph.value.end(), '-') == 4);
    CHECK(ph.entropy_bits > 90);
}

static void testStrength() {
    std::puts("strength: ordering");
    CHECK(kf::estimateStrength("password").level == 0);
    CHECK(kf::estimateStrength("aaaaaaaaaaaa").bits < kf::estimateStrength("k7#Qv9!mZ2rT").bits);
    CHECK(kf::estimateStrength("k7#Qv9!mZ2rT$wL4x&Bn").level >= 3);
    CHECK(kf::utf8::lower("ПрИвЕт Ёж ABC") == "привет ёж abc");
}

static void testVault() {
    std::puts("vault: create / reopen / tamper / wrong password");
    const fs::path dir = fs::temp_directory_path() / L"keyfox-tests";
    fs::remove_all(dir);
    const fs::path path = dir / L"тест vault.kfx";  // non-ASCII path on purpose
    const auto kdf = kf::crypto::kdfPreset("interactive");

    {
        auto v = kf::Vault::create(path, "correct horse battery staple", kdf);
        kf::Entry e;
        e.title = "GitHub";
        e.username = "alex@mail.ru";
        e.password = "k7#Qv9!mZ2rT";
        e.notes = "многострочные\nзаметки";
        e.tags = {"dev", "main"};
        e.favorite = true;
        v->add(e);
        v->save();
    }
    CHECK(fs::exists(path));

    {
        auto v = kf::Vault::open(path, "correct horse battery staple");
        CHECK(v->entries().size() == 1);
        const auto& e = v->entries()[0];
        CHECK(e.title == "GitHub" && e.password == "k7#Qv9!mZ2rT" && e.notes == "многострочные\nзаметки");
        CHECK(e.tags.size() == 2 && e.favorite && e.created > 0 && e.id.size() == 32);
    }

    CHECK(throws<kf::crypto::AuthError>([&] { kf::Vault::open(path, "wrong password"); }));

    // Flip one ciphertext byte -> authentication must fail.
    std::vector<char> bytes;
    {
        std::ifstream in(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(bytes.size() % 256 == (kf::crypto::kHeaderBytes + kf::crypto::kTagBytes) % 256);  // padded payload
    auto tampered = bytes;
    tampered[tampered.size() / 2] ^= 0x01;
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(tampered.data(), static_cast<std::streamsize>(tampered.size()));
    }
    CHECK(throws<kf::crypto::AuthError>([&] { kf::Vault::open(path, "correct horse battery staple"); }));

    // Header is authenticated too (salt byte changed).
    tampered = bytes;
    tampered[20] ^= 0x01;
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(tampered.data(), static_cast<std::streamsize>(tampered.size()));
    }
    CHECK(throws<kf::crypto::AuthError>([&] { kf::Vault::open(path, "correct horse battery staple"); }));

    // Absurd KDF parameters are rejected before any work is done.
    tampered = bytes;
    tampered[15] = 0x7F;  // memlimit ~ 2 TiB
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(tampered.data(), static_cast<std::streamsize>(tampered.size()));
    }
    CHECK(throws<kf::crypto::FormatError>([&] { kf::Vault::open(path, "correct horse battery staple"); }));

    // Master password change.
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    {
        auto v = kf::Vault::open(path, "correct horse battery staple");
        v->applyRekey(kf::Vault::prepareRekey("new master password", kdf));
        v->save();
    }
    CHECK(throws<kf::crypto::AuthError>([&] { kf::Vault::open(path, "correct horse battery staple"); }));
    CHECK(kf::Vault::open(path, "new master password")->entries().size() == 1);

    fs::remove_all(dir);
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    kf::crypto::init();
    testTotp();
    testGenerator();
    testStrength();
    testVault();
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
