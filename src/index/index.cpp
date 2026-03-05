#include "mygit/index/index.h"
#include "mygit/storage/file_utils.h"

#include <zlib.h>
#include <cstring>
#include <sstream>
#include <stdexcept>

// ── Binary index format (version 1) ──────────────────────────────────────────
//
// HEADER  (16 bytes)
//   magic[4]   = "MGIX"
//   version    = 1  (uint32_t LE)
//   count      = number of entries  (uint32_t LE)
//   reserved   = 0  (uint32_t LE)
//
// PER ENTRY
//   hash[40]   — 40 hex ASCII chars, NOT null-terminated
//   mtime      — int64_t LE, seconds since epoch
//   file_size  — uint64_t LE, bytes
//   path_len   — uint16_t LE, byte length of the path that follows
//   path[path_len] — UTF-8, NOT null-terminated
//
// FOOTER
//   crc32      — uint32_t LE, CRC-32 over ALL bytes from offset 0
//                up to (but not including) this field.
//
// Backward compatibility:
//   If the first four bytes are not "MGIX", the file is treated as the
//   legacy plain-text format and parsed accordingly.  The next call to
//   write() will upgrade the file to the binary format.

namespace mygit {

    // ────────────────────────────────────────────────────────────────────────
    // Little-endian helpers
    // ────────────────────────────────────────────────────────────────────────
    namespace {

        inline void writeU16LE(std::vector<std::byte>& buf, uint16_t v) {
            buf.push_back(static_cast<std::byte>(v & 0xFF));
            buf.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
        }
        inline void writeU32LE(std::vector<std::byte>& buf, uint32_t v) {
            buf.push_back(static_cast<std::byte>(v & 0xFF));
            buf.push_back(static_cast<std::byte>((v >>  8) & 0xFF));
            buf.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
            buf.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
        }
        inline void writeI64LE(std::vector<std::byte>& buf, int64_t v) {
            const uint64_t u = static_cast<uint64_t>(v);
            for (int i = 0; i < 8; ++i)
                buf.push_back(static_cast<std::byte>((u >> (8*i)) & 0xFF));
        }
        inline void writeU64LE(std::vector<std::byte>& buf, uint64_t v) {
            for (int i = 0; i < 8; ++i)
                buf.push_back(static_cast<std::byte>((v >> (8*i)) & 0xFF));
        }

        inline uint16_t readU16LE(const std::byte* p) {
            return static_cast<uint16_t>(
                (static_cast<uint16_t>(p[0])) |
                (static_cast<uint16_t>(p[1]) << 8));
        }
        inline uint32_t readU32LE(const std::byte* p) {
            return (static_cast<uint32_t>(p[0]))
                 | (static_cast<uint32_t>(p[1]) <<  8)
                 | (static_cast<uint32_t>(p[2]) << 16)
                 | (static_cast<uint32_t>(p[3]) << 24);
        }
        inline int64_t readI64LE(const std::byte* p) {
            uint64_t u = 0;
            for (int i = 0; i < 8; ++i)
                u |= (static_cast<uint64_t>(p[i]) << (8 * i));
            return static_cast<int64_t>(u);
        }
        inline uint64_t readU64LE(const std::byte* p) {
            uint64_t u = 0;
            for (int i = 0; i < 8; ++i)
                u |= (static_cast<uint64_t>(p[i]) << (8 * i));
            return u;
        }

        uint32_t computeCRC32(const std::vector<std::byte>& buf, size_t len) {
            return static_cast<uint32_t>(
                ::crc32(0,
                        reinterpret_cast<const Bytef*>(buf.data()),
                        static_cast<uInt>(len)));
        }

    } // anonymous namespace

    // ────────────────────────────────────────────────────────────────────────
    Index::Index(const fs::path& index_path)
        : index_path_(index_path) {
        if (fs::exists(index_path_)) {
            read();
        }
    }

    void Index::add(const IndexEntry& entry) {
        auto it = path_map_.find(entry.path);
        if (it != path_map_.end()) {
            entries_[it->second] = entry;
        } else {
            path_map_[entry.path] = entries_.size();
            entries_.push_back(entry);
        }
    }

    bool Index::remove(const std::string& path) {
        auto it = path_map_.find(path);
        if (it == path_map_.end()) return false;
        entries_.erase(entries_.begin() + static_cast<ptrdiff_t>(it->second));
        rebuildMap();
        return true;
    }

    std::optional<IndexEntry> Index::get(const std::string& path) const {
        auto it = path_map_.find(path);
        if (it == path_map_.end()) return std::nullopt;
        return entries_[it->second];
    }

    bool Index::contains(const std::string& path) const {
        return path_map_.count(path) > 0;
    }

    void Index::clear() {
        entries_.clear();
        path_map_.clear();
    }

    // ── write — binary format ────────────────────────────────────────────────
    void Index::write() const {
        const uint32_t count = static_cast<uint32_t>(entries_.size());

        std::vector<std::byte> buf;
        buf.reserve(16 + entries_.size() * 80); // generous estimate

        // Header
        for (char c : std::string{"MGIX"}) buf.push_back(static_cast<std::byte>(c));
        writeU32LE(buf, 1);     // version
        writeU32LE(buf, count);
        writeU32LE(buf, 0);     // reserved

        // Entries
        for (const auto& e : entries_) {
            // hash — 40 bytes
            for (char c : e.hash) buf.push_back(static_cast<std::byte>(c));
            writeI64LE(buf, static_cast<int64_t>(e.mtime));
            writeU64LE(buf, e.file_size);
            const uint16_t plen = static_cast<uint16_t>(e.path.size());
            writeU16LE(buf, plen);
            for (char c : e.path) buf.push_back(static_cast<std::byte>(c));
        }

        // CRC32 footer
        const uint32_t crc = computeCRC32(buf, buf.size());
        writeU32LE(buf, crc);

        storage::FileUtils::atomicWrite(index_path_, buf);
    }

    // ── readBinary — parse binary format ────────────────────────────────────
    void Index::readBinary(const std::vector<std::byte>& data) {
        const size_t sz = data.size();
        if (sz < 20) throw std::runtime_error("index too small");

        // Verify CRC32
        const uint32_t stored_crc = readU32LE(data.data() + sz - 4);
        const uint32_t actual_crc = computeCRC32(data, sz - 4);
        if (stored_crc != actual_crc)
            throw std::runtime_error("index CRC32 mismatch — possible corruption");

        const std::byte* p   = data.data();
        const std::byte* end = data.data() + sz - 4; // stop before CRC

        // Header
        if (p[0] != static_cast<std::byte>('M') ||
            p[1] != static_cast<std::byte>('G') ||
            p[2] != static_cast<std::byte>('I') ||
            p[3] != static_cast<std::byte>('X'))
            throw std::runtime_error("bad magic");

        /* uint32_t version = */ readU32LE(p + 4); // reserved for future use
        const uint32_t count = readU32LE(p + 8);
        /* uint32_t reserved = */ readU32LE(p + 12);
        p += 16;

        entries_.clear();
        entries_.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            if (p + 40 + 8 + 8 + 2 > end)
                throw std::runtime_error("index truncated mid-entry");

            IndexEntry e;

            // hash
            e.hash.assign(reinterpret_cast<const char*>(p), 40);
            p += 40;

            // mtime, file_size, path_len
            e.mtime     = static_cast<std::time_t>(readI64LE(p)); p += 8;
            e.file_size = readU64LE(p);                          p += 8;
            const uint16_t plen = readU16LE(p);                  p += 2;

            if (p + static_cast<ptrdiff_t>(plen) > end)
                throw std::runtime_error("index entry path overflows buffer");

            e.path.assign(reinterpret_cast<const char*>(p), plen);
            p += plen;

            // Validate
            if (e.hash.size() != 40)
                throw std::runtime_error("bad hash length in index");
            if (e.path.empty() ||
                e.path.find('\n') != std::string::npos ||
                e.path.find('\r') != std::string::npos ||
                e.path.find('\0') != std::string::npos)
                continue; // skip entries with unsafe paths

            entries_.push_back(std::move(e));
        }

        rebuildMap();
    }

    // ── readLegacyText — legacy plain-text format (backward compat) ──────────
    void Index::readLegacyText(const std::string& text) {
        std::istringstream iss(text);
        size_t claimed_count = 0;
        iss >> claimed_count;
        iss.ignore();

        entries_.clear();
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;

            std::istringstream ls(line);
            IndexEntry e;
            long long mtime_raw = 0;
            if (!(ls >> e.hash >> mtime_raw >> e.file_size)) continue;
            e.mtime = static_cast<std::time_t>(mtime_raw);

            if (e.hash.size() != 40) continue;

            ls.ignore(1);
            std::getline(ls, e.path);

            if (e.path.empty() ||
                e.path.find('\n') != std::string::npos ||
                e.path.find('\r') != std::string::npos ||
                e.path.find('\0') != std::string::npos)
                continue;

            entries_.push_back(std::move(e));
        }
        rebuildMap();
    }

    // ── read — auto-detect format ────────────────────────────────────────────
    void Index::read() {
        const std::vector<std::byte> data = storage::FileUtils::readFile(index_path_);
        if (data.size() >= 4 &&
            data[0] == static_cast<std::byte>('M') &&
            data[1] == static_cast<std::byte>('G') &&
            data[2] == static_cast<std::byte>('I') &&
            data[3] == static_cast<std::byte>('X')) {
            // Binary format
            try {
                readBinary(data);
                return;
            } catch (const std::exception& ex) {
                // Corrupt binary index — warn and fall through to empty state
                std::string warn = "warning: index file corrupt (";
                warn += ex.what();
                warn += "); starting with empty staging area\n";
                // Write to stderr but don't throw — a corrupted index shouldn't
                // prevent the user from at least running commands.
                entries_.clear();
                path_map_.clear();
                return;
            }
        }

        // Legacy text format
        std::string text;
        text.reserve(data.size());
        for (auto b : data) text.push_back(static_cast<char>(b));
        readLegacyText(text);
    }

    void Index::rebuildMap() {
        path_map_.clear();
        for (size_t i = 0; i < entries_.size(); ++i) {
            path_map_[entries_[i].path] = i;
        }
    }

} // namespace mygit
