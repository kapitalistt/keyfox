#pragma once
// Memory hygiene helpers built on libsodium.

#include <sodium.h>

#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace kf {

// Overwrites a string's characters before clearing it.
inline void wipe(std::string& s) noexcept {
    if (!s.empty()) sodium_memzero(s.data(), s.size());
    s.clear();
}

// std::allocator that zeroes every block before giving it back to the heap,
// so reallocations of plaintext buffers never leave copies behind.
template <class T>
struct ZeroingAllocator {
    using value_type = T;
    ZeroingAllocator() noexcept = default;
    template <class U>
    ZeroingAllocator(const ZeroingAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
    void deallocate(T* p, std::size_t n) noexcept {
        sodium_memzero(p, n * sizeof(T));
        std::allocator<T>{}.deallocate(p, n);
    }
    template <class U>
    bool operator==(const ZeroingAllocator<U>&) const noexcept { return true; }
};

using SecureBytes = std::vector<unsigned char, ZeroingAllocator<unsigned char>>;

// Key storage: sodium_malloc gives guard pages, a canary, mlock() and zero-on-free.
// The region is kept PAGE_NOACCESS while idle; use ReadAccess to touch it.
class SecureBuffer {
public:
    SecureBuffer() = default;
    explicit SecureBuffer(std::size_t n) : size_(n) {
        data_ = static_cast<unsigned char*>(sodium_malloc(n ? n : 1));
        if (!data_) throw std::bad_alloc();
    }
    ~SecureBuffer() { reset(); }

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    SecureBuffer(SecureBuffer&& o) noexcept
        : data_(std::exchange(o.data_, nullptr)), size_(std::exchange(o.size_, 0)) {}
    SecureBuffer& operator=(SecureBuffer&& o) noexcept {
        if (this != &o) {
            reset();
            data_ = std::exchange(o.data_, nullptr);
            size_ = std::exchange(o.size_, 0);
        }
        return *this;
    }

    void reset() noexcept {
        if (data_) {
            sodium_mprotect_readwrite(data_);
            sodium_free(data_);
            data_ = nullptr;
            size_ = 0;
        }
    }

    unsigned char* data() noexcept { return data_; }
    const unsigned char* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return data_ == nullptr; }

    void protect() noexcept { if (data_) sodium_mprotect_noaccess(data_); }
    void allowRead() noexcept { if (data_) sodium_mprotect_readonly(data_); }
    void allowWrite() noexcept { if (data_) sodium_mprotect_readwrite(data_); }

private:
    unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
};

// RAII: makes a protected SecureBuffer readable for the current scope.
class ReadAccess {
public:
    explicit ReadAccess(SecureBuffer& b) : b_(b) { b_.allowRead(); }
    ~ReadAccess() { b_.protect(); }
    ReadAccess(const ReadAccess&) = delete;
    ReadAccess& operator=(const ReadAccess&) = delete;

private:
    SecureBuffer& b_;
};

}  // namespace kf
