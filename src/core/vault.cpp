#include "core/vault.hpp"

#include "platform/win.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>

namespace kf {

namespace fs = std::filesystem;

namespace {

constexpr unsigned char kPayloadMagic[4] = {'K', 'F', 'D', 'B'};
constexpr std::uint16_t kPayloadVersion = 1;
constexpr std::size_t kPadBlock = 256;  // hides exact payload length
constexpr std::uint32_t kMaxField = 16u * 1024u * 1024u;

class Writer {
public:
    explicit Writer(SecureBytes& out) : out_(out) {}
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        out_.insert(out_.end(), b, b + n);
    }
    void u8(std::uint8_t v) { out_.push_back(v); }
    void u16(std::uint16_t v) { for (int i = 0; i < 2; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i))); }
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i))); }
    void i64(std::int64_t v) {
        const auto u = static_cast<std::uint64_t>(v);
        for (int i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(u >> (8 * i)));
    }
    void str(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(s.data(), s.size());
    }

private:
    SecureBytes& out_;
};

class Reader {
public:
    Reader(const unsigned char* p, std::size_t n) : p_(p), n_(n) {}
    void need(std::size_t k) const {
        if (n_ - pos_ < k) throw crypto::FormatError("payload is truncated");
    }
    std::uint8_t u8() { need(1); return p_[pos_++]; }
    std::uint16_t u16() { std::uint16_t v = 0; for (int i = 0; i < 2; ++i) v |= static_cast<std::uint16_t>(u8() << (8 * i)); return v; }
    std::uint32_t u32() { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(u8()) << (8 * i); return v; }
    std::int64_t i64() {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(u8()) << (8 * i);
        return static_cast<std::int64_t>(v);
    }
    std::string str() {
        const std::uint32_t len = u32();
        if (len > kMaxField) throw crypto::FormatError("field is too large");
        need(len);
        std::string s(reinterpret_cast<const char*>(p_ + pos_), len);
        pos_ += len;
        return s;
    }
    bool done() const { return pos_ == n_; }

private:
    const unsigned char* p_;
    std::size_t n_;
    std::size_t pos_ = 0;
};

std::vector<unsigned char> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + win::pathUtf8(path));
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFileAtomic(const fs::path& path, const std::vector<unsigned char>& data) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());

    fs::path tmp = path;
    tmp += L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot write " + win::pathUtf8(tmp));

    DWORD written = 0;
    const BOOL ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    const BOOL flushed = FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !flushed || written != data.size()) {
        DeleteFileW(tmp.c_str());
        throw std::runtime_error("write failed: " + win::pathUtf8(tmp));
    }

    std::error_code ec;
    if (fs::exists(path, ec)) {
        fs::path bak = path;
        bak += L".bak";
        CopyFileW(path.c_str(), bak.c_str(), FALSE);
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("cannot replace " + win::pathUtf8(path));
}

}  // namespace

void Entry::wipeAll() noexcept {
    for (auto* s : {&id, &title, &username, &password, &url, &notes, &totp, &folder}) wipe(*s);
    for (auto& t : tags) wipe(t);
    tags.clear();
}

Vault::~Vault() {
    for (auto& e : entries_) e.wipeAll();
}

std::string Vault::newId() {
    unsigned char raw[16];
    randombytes_buf(raw, sizeof raw);
    char hex[sizeof raw * 2 + 1];
    sodium_bin2hex(hex, sizeof hex, raw, sizeof raw);
    return hex;
}

SecureBytes Vault::serialize(const std::vector<Entry>& entries) {
    SecureBytes out;
    out.reserve(4096);
    Writer w(out);
    w.raw(kPayloadMagic, 4);
    w.u16(kPayloadVersion);
    w.u32(static_cast<std::uint32_t>(entries.size()));
    for (const auto& e : entries) {
        w.str(e.id);
        w.str(e.title);
        w.str(e.username);
        w.str(e.password);
        w.str(e.url);
        w.str(e.notes);
        w.str(e.totp);
        w.str(e.folder);
        w.u32(static_cast<std::uint32_t>(e.tags.size()));
        for (const auto& t : e.tags) w.str(t);
        w.u8(e.favorite ? 1 : 0);
        w.i64(e.created);
        w.i64(e.modified);
    }

    const std::size_t unpadded = out.size();
    out.resize(unpadded + kPadBlock);
    std::size_t padded = 0;
    if (sodium_pad(&padded, out.data(), unpadded, kPadBlock, out.size()) != 0)
        throw std::runtime_error("padding failed");
    out.resize(padded);
    return out;
}

std::vector<Entry> Vault::deserialize(const SecureBytes& payload) {
    std::size_t len = 0;
    if (sodium_unpad(&len, payload.data(), payload.size(), kPadBlock) != 0)
        throw crypto::FormatError("bad payload padding");

    Reader r(payload.data(), len);
    r.need(4);
    for (unsigned char m : kPayloadMagic)
        if (r.u8() != m) throw crypto::FormatError("bad payload magic");
    if (r.u16() != kPayloadVersion) throw crypto::FormatError("unsupported payload version");

    const std::uint32_t count = r.u32();
    std::vector<Entry> entries;
    entries.reserve(std::min<std::uint32_t>(count, 100000));
    for (std::uint32_t i = 0; i < count; ++i) {
        Entry e;
        e.id = r.str();
        e.title = r.str();
        e.username = r.str();
        e.password = r.str();
        e.url = r.str();
        e.notes = r.str();
        e.totp = r.str();
        e.folder = r.str();
        const std::uint32_t tags = r.u32();
        if (tags > 1024) throw crypto::FormatError("too many tags");
        for (std::uint32_t t = 0; t < tags; ++t) e.tags.push_back(r.str());
        e.favorite = r.u8() != 0;
        e.created = r.i64();
        e.modified = r.i64();
        entries.push_back(std::move(e));
    }
    if (!r.done()) throw crypto::FormatError("trailing payload data");
    return entries;
}

std::unique_ptr<Vault> Vault::create(const fs::path& path, std::string_view password,
                                     const crypto::KdfParams& kdf) {
    std::unique_ptr<Vault> v(new Vault());
    v->path_ = path;
    v->header_.kdf = kdf;
    v->header_.salt = crypto::randomSalt();
    v->key_ = crypto::deriveKey(password, v->header_.salt, kdf);
    v->save();
    return v;
}

std::unique_ptr<Vault> Vault::open(const fs::path& path, std::string_view password) {
    const auto file = readFile(path);
    std::unique_ptr<Vault> v(new Vault());
    v->path_ = path;
    v->header_ = crypto::parseHeader(file);
    v->key_ = crypto::deriveKey(password, v->header_.salt, v->header_.kdf);
    const SecureBytes plain = crypto::open(file, v->key_);
    v->entries_ = deserialize(plain);
    return v;
}

Vault::Rekey Vault::prepareRekey(std::string_view password, const crypto::KdfParams& kdf) {
    Rekey r;
    r.kdf = kdf;
    r.salt = crypto::randomSalt();
    r.key = crypto::deriveKey(password, r.salt, kdf);
    return r;
}

void Vault::applyRekey(Rekey&& r) {
    header_.kdf = r.kdf;
    header_.salt = r.salt;
    key_ = std::move(r.key);
}

void Vault::save() {
    const SecureBytes plain = serialize(entries_);
    const auto file = crypto::seal(plain, key_, header_);
    writeFileAtomic(path_, file);
}

Entry* Vault::find(std::string_view id) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id; });
    return it == entries_.end() ? nullptr : &*it;
}

Entry& Vault::add(Entry e) {
    if (e.id.empty()) e.id = newId();
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    if (e.created == 0) e.created = now;
    e.modified = now;
    entries_.push_back(std::move(e));
    return entries_.back();
}

void Vault::remove(std::string_view id) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id; });
    if (it != entries_.end()) entries_.erase(it);
}

}  // namespace kf
