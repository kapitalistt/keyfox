#pragma once
// In-memory vault: entries plus the key needed to write them back to disk.

#include "core/crypto.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kf {

struct Entry {
    std::string id;
    std::string title;
    std::string username;
    std::string password;
    std::string url;
    std::string notes;
    std::string totp;    // base32 secret or otpauth:// URI
    std::string folder;
    std::vector<std::string> tags;
    bool favorite = false;
    std::int64_t created = 0;
    std::int64_t modified = 0;

    Entry() = default;
    Entry(const Entry&) = default;
    Entry(Entry&&) noexcept = default;
    Entry& operator=(const Entry&) = default;
    Entry& operator=(Entry&&) noexcept = default;
    ~Entry() { wipeAll(); }

    void wipeAll() noexcept;
};

class Vault {
public:
    // Slow (Argon2id). Safe to call from a worker thread.
    static std::unique_ptr<Vault> create(const std::filesystem::path& path, std::string_view password,
                                         const crypto::KdfParams& kdf);
    static std::unique_ptr<Vault> open(const std::filesystem::path& path, std::string_view password);

    // A new key + salt for a master password change. Slow; run on a worker thread,
    // then hand the result to applyRekey() and call save().
    struct Rekey {
        crypto::Salt salt{};
        crypto::KdfParams kdf;
        SecureBuffer key;
    };
    static Rekey prepareRekey(std::string_view password, const crypto::KdfParams& kdf);
    void applyRekey(Rekey&& r);

    ~Vault();
    Vault(const Vault&) = delete;
    Vault& operator=(const Vault&) = delete;

    // Atomic: writes vault.kfx.tmp, keeps the previous file as vault.kfx.bak, then renames.
    void save();

    std::vector<Entry>& entries() { return entries_; }
    const std::vector<Entry>& entries() const { return entries_; }
    Entry* find(std::string_view id);
    Entry& add(Entry e);
    void remove(std::string_view id);

    const std::filesystem::path& path() const { return path_; }
    const crypto::KdfParams& kdf() const { return header_.kdf; }

    static std::string newId();

    // Payload (de)serialisation, exposed for tests.
    static SecureBytes serialize(const std::vector<Entry>& entries);
    static std::vector<Entry> deserialize(const SecureBytes& payload);

private:
    Vault() = default;

    std::filesystem::path path_;
    crypto::Header header_;
    SecureBuffer key_;
    std::vector<Entry> entries_;
};

}  // namespace kf
