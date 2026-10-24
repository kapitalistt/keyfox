#pragma once

#include "core/crypto.hpp"

#include <filesystem>

namespace kf {

struct AppOptions {
    std::filesystem::path vault_path;
    crypto::KdfParams kdf;          // used for new vaults and master password changes
    bool kdf_explicit = false;      // --kdf given: also applies when changing the master password
    int autolock_seconds = 300;
    int clipboard_seconds = 15;
    bool animations = true;
};

// Runs the full-screen TUI. Returns the process exit code.
int runApp(const AppOptions& options);

}  // namespace kf
