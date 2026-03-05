#include "mygit/storage/object_store.h"
#include "mygit/storage/file_utils.h"
#include "mygit/utils/compression.h"

#include <zlib.h>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

// ── Pack-file format (version 1) ─────────────────────────────────────────────
//
// PACK FILE  (objects/pack.mgpk)
//   Header : "MGPK" (4 B) | version uint32 LE | count uint32 LE
//   Per obj: hash[40] | data_len uint32 LE | compressed_data (data_len bytes)
//   Footer : CRC32 uint32 LE  (over all preceding bytes)
//
// INDEX FILE (objects/pack.mgpk.idx)
//   Header : "MGPI" (4 B) | version uint32 LE | count uint32 LE
//   Per obj: hash[40] | offset uint64 LE   (sorted by hash for binary search)
//   Footer : CRC32 uint32 LE

namespace mygit {

    // ── little-endian helpers ────────────────────────────────────────────────
    namespace {

        inline uint32_t readU32LE(const std::byte* p) {
            return (static_cast<uint32_t>(p[0]))
                 | (static_cast<uint32_t>(p[1]) <<  8)
                 | (static_cast<uint32_t>(p[2]) << 16)
                 | (static_cast<uint32_t>(p[3]) << 24);
        }
        inline uint64_t readU64LE(const std::byte* p) {
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i)
                v |= (static_cast<uint64_t>(p[i]) << (8 * i));
            return v;
        }
        inline void writeU32LE(std::vector<std::byte>& b, uint32_t v) {
            b.push_back(static_cast<std::byte>(v & 0xFF));
            b.push_back(static_cast<std::byte>((v >>  8) & 0xFF));
            b.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
            b.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
        }
        inline void writeU64LE(std::vector<std::byte>& b, uint64_t v) {
            for (int i = 0; i < 8; ++i)
                b.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
        }
        inline void writeBytes(std::vector<std::byte>& b, const char* s, size_t n) {
            for (size_t i = 0; i < n; ++i)
                b.push_back(static_cast<std::byte>(s[i]));
        }
        uint32_t computeCRC(const std::vector<std::byte>& buf, size_t len) {
            return static_cast<uint32_t>(
                ::crc32(0,
                        reinterpret_cast<const Bytef*>(buf.data()),
                        static_cast<uInt>(len)));
        }

    } // anonymous namespace

    // ── constructor ──────────────────────────────────────────────────────────
    ObjectStore::ObjectStore(const fs::path& objects_dir)
        : objects_dir_(objects_dir) {
        storage::FileUtils::ensureDirectory(objects_dir_);
    }

    // ── store ────────────────────────────────────────────────────────────────
    std::string ObjectStore::store(const Object& obj) {
        const std::string hash = obj.hash();
        if (exists(hash)) return hash; // already stored

        std::string type_str;
        switch (obj.type()) {
            case ObjectType::Blob:   type_str = "blob";   break;
            case ObjectType::Tree:   type_str = "tree";   break;
            case ObjectType::Commit: type_str = "commit"; break;
        }

        const auto content = obj.serialize();
        std::string header = type_str + " " + std::to_string(content.size()) + '\0';

        std::vector<std::byte> full;
        full.reserve(header.size() + content.size());
        for (char c : header) full.push_back(static_cast<std::byte>(c));
        full.insert(full.end(), content.begin(), content.end());

        const auto compressed = utils::Compression::compress(full);
        storage::FileUtils::writeFile(getObjectPath(hash), compressed);

        return hash;
    }

    // ── load ─────────────────────────────────────────────────────────────────
    std::optional<std::vector<std::byte>> ObjectStore::load(const std::string& hash) const {
        if (hash.size() < 4) return std::nullopt;

        // 1. Try loose object
        const fs::path path = getObjectPath(hash);
        if (fs::exists(path)) {
            const auto compressed = storage::FileUtils::readFile(path);
            const auto full = utils::Compression::decompress(compressed);
            for (size_t i = 0; i < full.size(); ++i) {
                if (full[i] == static_cast<std::byte>('\0')) {
                    return std::vector<std::byte>(
                        full.begin() + static_cast<ptrdiff_t>(i) + 1, full.end());
                }
            }
            return std::nullopt;
        }

        // 2. Try pack file
        return loadFromPack(hash);
    }

    // ── exists ────────────────────────────────────────────────────────────────
    bool ObjectStore::exists(const std::string& hash) const {
        if (hash.size() < 4) return false;
        if (fs::exists(getObjectPath(hash))) return true;
        ensurePackIndex();
        return pack_index_cache_.count(hash) > 0;
    }

    // ── getObjectPath ────────────────────────────────────────────────────────
    fs::path ObjectStore::getObjectPath(const std::string& hash) const {
        auto [dir, file] = splitHash(hash);
        return objects_dir_ / dir / file;
    }

    std::pair<std::string, std::string> ObjectStore::splitHash(const std::string& hash) const {
        if (hash.size() < 4) throw std::invalid_argument("Hash too short: " + hash);
        return { hash.substr(0, 2), hash.substr(2) };
    }

    // ── ensurePackIndex ───────────────────────────────────────────────────────
    void ObjectStore::ensurePackIndex() const {
        if (pack_index_loaded_) return;
        pack_index_loaded_ = true; // mark before loading so recursive calls are safe

        const fs::path idx_path = objects_dir_ / "pack.mgpk.idx";
        if (!fs::exists(idx_path)) return;

        try {
            const auto data = storage::FileUtils::readFile(idx_path);
            const size_t sz = data.size();

            // Minimum: 4 magic + 4 version + 4 count + 4 crc = 16 bytes
            if (sz < 16) return;

            const std::byte* p = data.data();

            // Verify magic
            if (p[0] != static_cast<std::byte>('M') ||
                p[1] != static_cast<std::byte>('G') ||
                p[2] != static_cast<std::byte>('P') ||
                p[3] != static_cast<std::byte>('I')) return;

            // Verify CRC
            const uint32_t stored_crc = readU32LE(data.data() + sz - 4);
            const uint32_t actual_crc = computeCRC(data, sz - 4);
            if (stored_crc != actual_crc) return; // corrupt index

            const uint32_t count = readU32LE(p + 8);
            p += 12; // skip magic + version + count

            constexpr size_t ENTRY_SIZE = 40 + 8; // hash + offset
            if (sz < 12 + count * ENTRY_SIZE + 4) return;

            for (uint32_t i = 0; i < count; ++i) {
                std::string h(reinterpret_cast<const char*>(p), 40);
                const uint64_t offset = readU64LE(p + 40);
                pack_index_cache_.emplace(std::move(h), offset);
                p += ENTRY_SIZE;
            }
        } catch (...) {
            // Corrupt or unreadable index — leave cache empty
        }
    }

    // ── loadFromPack ──────────────────────────────────────────────────────────
    std::optional<std::vector<std::byte>>
    ObjectStore::loadFromPack(const std::string& hash) const {
        ensurePackIndex();

        auto it = pack_index_cache_.find(hash);
        if (it == pack_index_cache_.end()) return std::nullopt;

        const fs::path pack_path = objects_dir_ / "pack.mgpk";
        if (!fs::exists(pack_path)) return std::nullopt;

        try {
            const auto pack = storage::FileUtils::readFile(pack_path);
            const size_t sz = pack.size();
            const uint64_t offset = it->second;

            // At offset: hash[40] | data_len uint32 | compressed_data
            if (offset + 44 > sz) return std::nullopt;
            const std::byte* entry = pack.data() + offset;

            const uint32_t data_len = readU32LE(entry + 40);
            if (offset + 44 + data_len > sz) return std::nullopt;

            // Copy compressed bytes
            std::vector<std::byte> compressed(
                pack.begin() + static_cast<ptrdiff_t>(offset) + 44,
                pack.begin() + static_cast<ptrdiff_t>(offset) + 44 + data_len);

            const auto full = utils::Compression::decompress(compressed);

            // Strip the "<type> <size>\0" header
            for (size_t i = 0; i < full.size(); ++i) {
                if (full[i] == static_cast<std::byte>('\0')) {
                    return std::vector<std::byte>(
                        full.begin() + static_cast<ptrdiff_t>(i) + 1, full.end());
                }
            }
        } catch (...) {}

        return std::nullopt;
    }

    // ── packObjects ───────────────────────────────────────────────────────────
    std::size_t ObjectStore::packObjects() {
        // Enumerate all loose objects
        std::vector<std::pair<std::string, fs::path>> loose;

        std::error_code ec;
        for (const auto& de : fs::directory_iterator(objects_dir_, ec)) {
            if (!de.is_directory()) continue;
            const std::string dir_name = de.path().filename().string();
            if (dir_name.size() != 2) continue;
            for (const auto& fe : fs::directory_iterator(de.path(), ec)) {
                if (!fe.is_regular_file()) continue;
                const std::string file_name = fe.path().filename().string();
                const std::string hash = dir_name + file_name;
                loose.push_back({hash, fe.path()});
            }
        }

        if (loose.empty()) return 0;

        // Sort by hash (required for binary search in the index)
        std::sort(loose.begin(), loose.end());

        // ── Build pack file ────────────────────────────────────────────────
        std::vector<std::byte> pack_buf;
        pack_buf.reserve(1 << 20); // 1 MB initial
        writeBytes(pack_buf, "MGPK", 4);
        writeU32LE(pack_buf, 1); // version
        writeU32LE(pack_buf, static_cast<uint32_t>(loose.size()));

        // ── Build index entries ────────────────────────────────────────────
        std::vector<std::pair<std::string /*hash*/, uint64_t /*offset*/>> idx_entries;
        idx_entries.reserve(loose.size());

        for (const auto& [hash, path] : loose) {
            const uint64_t offset = pack_buf.size();
            idx_entries.push_back({hash, offset});

            // Write hash
            for (char c : hash) pack_buf.push_back(static_cast<std::byte>(c));

            // Write compressed data
            const auto data = storage::FileUtils::readFile(path);
            writeU32LE(pack_buf, static_cast<uint32_t>(data.size()));
            pack_buf.insert(pack_buf.end(), data.begin(), data.end());
        }

        const uint32_t pack_crc = computeCRC(pack_buf, pack_buf.size());
        writeU32LE(pack_buf, pack_crc);

        // ── Build index file ───────────────────────────────────────────────
        std::vector<std::byte> idx_buf;
        idx_buf.reserve(12 + loose.size() * 48 + 4);
        writeBytes(idx_buf, "MGPI", 4);
        writeU32LE(idx_buf, 1); // version
        writeU32LE(idx_buf, static_cast<uint32_t>(idx_entries.size()));

        for (const auto& [hash, off] : idx_entries) {
            for (char c : hash) idx_buf.push_back(static_cast<std::byte>(c));
            writeU64LE(idx_buf, off);
        }

        const uint32_t idx_crc = computeCRC(idx_buf, idx_buf.size());
        writeU32LE(idx_buf, idx_crc);

        // ── Atomic write both files ────────────────────────────────────────
        const fs::path pack_path = objects_dir_ / "pack.mgpk";
        const fs::path idx_path  = objects_dir_ / "pack.mgpk.idx";

        storage::FileUtils::atomicWrite(pack_path, pack_buf);
        storage::FileUtils::atomicWrite(idx_path,  idx_buf);

        // ── Remove loose files ─────────────────────────────────────────────
        for (const auto& [hash, path] : loose) {
            std::error_code e2;
            fs::remove(path, e2);
            fs::remove(path.parent_path(), e2); // removes dir if empty
        }

        // Refresh the in-memory index cache
        pack_index_cache_.clear();
        pack_index_loaded_ = false;

        return loose.size();
    }

    // ── packCount ─────────────────────────────────────────────────────────────
    std::size_t ObjectStore::packCount() const {
        ensurePackIndex();
        return pack_index_cache_.size();
    }

} // namespace mygit

