#pragma once
// Clipboard with automatic clearing.
//
// Copied secrets are tagged so that Windows clipboard history (Win+V), cloud
// clipboard sync and clipboard monitors skip them, and they are wiped after a
// timeout, but only if the clipboard still holds what we put there.

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace kf {

class Clipboard {
public:
    Clipboard();
    ~Clipboard();
    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;

    bool copy(const std::string& utf8, int clear_after_seconds);
    void clearIfOurs();
    // Seconds until auto-clear, or -1 when nothing is pending.
    int secondsLeft() const;

private:
    void worker();

    void* hwnd_ = nullptr;
    mutable std::mutex m_;
    std::condition_variable cv_;
    unsigned long seq_ = 0;
    bool armed_ = false;
    bool stop_ = false;
    std::chrono::steady_clock::time_point deadline_{};
    std::thread thread_;
};

}  // namespace kf
