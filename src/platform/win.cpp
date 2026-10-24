#include "platform/win.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>

namespace kf::win {

std::wstring toWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string toUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string pathUtf8(const std::filesystem::path& p) { return toUtf8(p.native()); }

std::filesystem::path defaultVaultPath() {
    PWSTR appdata = nullptr;
    std::filesystem::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata)))
        base = appdata;
    else
        base = std::filesystem::current_path();
    CoTaskMemFree(appdata);
    return base / L"KeyFox" / L"vault.kfx";
}

void setupProcess() {
    // Never load DLLs from the current directory (DLL planting).
    SetDllDirectoryW(L"");
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"KeyFox");
}

bool capsLockOn() { return (GetKeyState(VK_CAPITAL) & 1) != 0; }

bool openUrl(std::string_view url) {
    std::string u(url);
    u.erase(std::remove_if(u.begin(), u.end(), [](char c) { return c == ' ' || c == '\t'; }), u.end());
    if (u.empty()) return false;
    if (u.find("://") == std::string::npos) u = "https://" + u;
    if (u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0) return false;
    const auto w = toWide(u);
    const auto rc = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return rc > 32;
}

}  // namespace kf::win
