#pragma once

#include "mygit/core/object.h"
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace mygit {
    //Object storage manager
    class ObjectStore {
        public:
            explicit ObjectStore(const fs::path& objects_dir);

            // Store object (always to a loose file)
            std::string store(const Object& obj);

            // Load object raw content (payload after the type/size header) by hash.
            // Checks loose objects first, then the pack file.
            std::optional<std::vector<std::byte>> load(const std::string& hash) const;

            // Check if object exists (loose or in pack)
            bool exists(const std::string& hash) const;

            //Get object path (for loose objects)
            fs::path getObjectPath(const std::string& hash) const;

            // ── pack-file support ─────────────────────────────────────────
            // Pack all loose objects into objects/pack.mgpk + objects/pack.mgpk.idx,
            // then delete the loose files.  No-op if there are no loose objects.
            // Returns the number of objects packed.
            std::size_t packObjects();

            // Number of objects in the pack file (0 if none).
            std::size_t packCount() const;

        private:
            fs::path objects_dir_;

            // ── pack cache ────────────────────────────────────────────────
            // Lazily loaded: maps hash → byte offset in pack file.
            // Marked mutable so load() / exists() can populate it on first use.
            mutable std::unordered_map<std::string, std::uint64_t> pack_index_cache_;
            mutable bool pack_index_loaded_ = false;

            std::pair<std::string, std::string> splitHash(const std::string& hash) const;

            // Load a single object from the pack file at a known offset.
            std::optional<std::vector<std::byte>>
            loadFromPack(const std::string& hash) const;

            // Ensure pack_index_cache_ is populated (no-op if already done).
            void ensurePackIndex() const;
    };
}
