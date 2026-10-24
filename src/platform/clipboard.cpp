#include "platform/clipboard.hpp"

#include "platform/win.hpp"

#include <windows.h>

#include <cstring>

namespace kf {

namespace {

bool openClipboard(HWND owner) {
    for (int i = 0; i < 20; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(15);
    }
    return false;
}

void setDword(const wchar_t* format, DWORD value) {
    const UINT f = RegisterClipboardFormatW(format);
    if (!f) return;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!h) return;
    *static_cast<DWORD*>(GlobalLock(h)) = value;
    GlobalUnlock(h);
    if (!SetClipboardData(f, h)) GlobalFree(h);
}

}  // namespace

Clipboard::Clipboard() : thread_([this] { worker(); }) {}

Clipboard::~Clipboard() {
    {
        std::lock_guard lk(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    clearIfOurs();
    if (hwnd_) DestroyWindow(static_cast<HWND>(hwnd_));
}

bool Clipboard::copy(const std::string& utf8, int clear_after_seconds) {
    // SetClipboardData needs an owner window after EmptyClipboard; a hidden
    // message-only window is enough.
    if (!hwnd_)
        hwnd_ = CreateWindowExW(0, L"STATIC", L"KeyFoxClipboard", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);

    std::wstring w = win::toWide(utf8);
    const std::size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL text = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!text) {
        SecureZeroMemory(w.data(), w.size() * sizeof(wchar_t));
        return false;
    }
    std::memcpy(GlobalLock(text), w.c_str(), bytes);
    GlobalUnlock(text);
    SecureZeroMemory(w.data(), w.size() * sizeof(wchar_t));

    if (!openClipboard(static_cast<HWND>(hwnd_))) {
        GlobalFree(text);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_UNICODETEXT, text) != nullptr;
    if (!ok) GlobalFree(text);

    // Keep secrets out of Win+V history, cloud sync and clipboard managers.
    if (const UINT ex = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing")) {
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, 1)) {
            if (!SetClipboardData(ex, h)) GlobalFree(h);
        }
    }
    setDword(L"CanIncludeInClipboardHistory", 0);
    setDword(L"CanUploadToCloudClipboard", 0);
    CloseClipboard();
    if (!ok) return false;

    {
        std::lock_guard lk(m_);
        seq_ = GetClipboardSequenceNumber();
        armed_ = clear_after_seconds > 0;
        deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(clear_after_seconds);
    }
    cv_.notify_all();
    return true;
}

void Clipboard::clearIfOurs() {
    unsigned long seq;
    {
        std::lock_guard lk(m_);
        seq = seq_;
        armed_ = false;
    }
    if (seq == 0 || GetClipboardSequenceNumber() != seq) return;  // user copied something else
    if (openClipboard(nullptr)) {
        EmptyClipboard();
        CloseClipboard();
    }
    std::lock_guard lk(m_);
    seq_ = 0;
}

int Clipboard::secondsLeft() const {
    std::lock_guard lk(m_);
    if (!armed_) return -1;
    const auto left = std::chrono::duration_cast<std::chrono::seconds>(deadline_ - std::chrono::steady_clock::now());
    return static_cast<int>(left.count()) + 1;
}

void Clipboard::worker() {
    std::unique_lock lk(m_);
    while (!stop_) {
        if (!armed_) {
            cv_.wait(lk);
            continue;
        }
        if (cv_.wait_until(lk, deadline_) == std::cv_status::timeout && armed_ && !stop_ &&
            std::chrono::steady_clock::now() >= deadline_) {
            lk.unlock();
            clearIfOurs();
            lk.lock();
        }
    }
}

}  // namespace kf
