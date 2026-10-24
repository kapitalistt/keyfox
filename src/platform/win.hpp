#pragma once
// Thin Win32 helpers. Keeps <windows.h> out of the UI code.

#include <filesystem>
#include <string>
#include <string_view>

namespace kf::win {

std::wstring toWide(std::string_view utf8);
std::string toUtf8(std::wstring_view wide);
std::string pathUtf8(const std::filesystem::path& p);

// %APPDATA%\KeyFox\vault.kfx
std::filesystem::path defaultVaultPath();

// UTF-8 console, window title, safer DLL search order, heap hardening.
void setupProcess();

bool capsLockOn();

// Opens http(s) links in the default browser. Returns false for anything else.
bool openUrl(std::string_view url);

}  // namespace kf::win
