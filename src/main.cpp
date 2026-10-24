// KeyFox - console password manager for Windows.

#include "core/crypto.hpp"
#include "platform/win.hpp"
#include "ui/app.hpp"
#include "ui/i18n.hpp"

#include <cstdio>
#include <exception>
#include <string>

namespace {

constexpr const char* kVersion = "1.0.0";

void printUsage() {
    std::puts(
        "KeyFox " "1.0.0" " - console password manager\n"
        "\n"
        "Usage: keyfox [vault.kfx] [options]\n"
        "\n"
        "  vault.kfx              vault file (default: %APPDATA%\\KeyFox\\vault.kfx)\n"
        "  --lang en|ru           interface language (default: en, F2 switches at runtime)\n"
        "  --kdf interactive|moderate|strong\n"
        "                         Argon2id cost for new vaults / password changes\n"
        "                         interactive = 64 MiB t=2, moderate = 256 MiB t=3 (default),\n"
        "                         strong = 1 GiB t=4\n"
        "  --autolock SECONDS     lock after inactivity (default 300)\n"
        "  --clip SECONDS         clear copied secrets from the clipboard (default 15)\n"
        "  --no-anim              disable animations\n"
        "  --version              print version\n"
        "  --help                 this help\n");
}

int toInt(const std::string& s, int fallback, int lo, int hi) {
    try {
        const int v = std::stoi(s);
        return v < lo ? lo : v > hi ? hi : v;
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    kf::win::setupProcess();

    kf::AppOptions opt;
    opt.vault_path = kf::win::defaultVaultPath();
    opt.kdf = kf::crypto::kdfPreset("moderate");

    for (int i = 1; i < argc; ++i) {
        const std::string a = kf::win::toUtf8(argv[i]);
        auto next = [&]() -> std::string { return i + 1 < argc ? kf::win::toUtf8(argv[++i]) : std::string(); };
        if (a == "--help" || a == "-h" || a == "/?") {
            printUsage();
            return 0;
        } else if (a == "--version" || a == "-v") {
            std::printf("KeyFox %s\n", kVersion);
            return 0;
        } else if (a == "--lang") {
            kf::currentLang() = next() == "ru" ? kf::Lang::Ru : kf::Lang::En;
        } else if (a == "--kdf") {
            const std::string k = next();
            if (k != "interactive" && k != "moderate" && k != "strong") {
                std::fprintf(stderr, "unknown --kdf value: %s\n", k.c_str());
                return 2;
            }
            opt.kdf = kf::crypto::kdfPreset(k);
            opt.kdf_explicit = true;
        } else if (a == "--autolock") {
            opt.autolock_seconds = toInt(next(), 300, 30, 86400);
        } else if (a == "--clip") {
            opt.clipboard_seconds = toInt(next(), 15, 0, 600);
        } else if (a == "--no-anim") {
            opt.animations = false;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s (see --help)\n", a.c_str());
            return 2;
        } else {
            opt.vault_path = argv[i];
        }
    }

    try {
        kf::crypto::init();
        return kf::runApp(opt);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "KeyFox: fatal error: %s\n", ex.what());
        return 1;
    }
}
