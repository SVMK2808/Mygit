#pragma once

#include "mygit/core/types.h"
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace fs = std::filesystem;

namespace mygit {

    // Manages the Git staging area (index file)
    class Index {
    public:
        explicit Index(const fs::path& index_path);

        // Stage a file: add or update its entry
        void add(const IndexEntry& entry);

        // Remove a file from the index by path
        bool remove(const std::string& path);

        // Look up an entry by relative path
        std::optional<IndexEntry> get(const std::string& path) const;

        // Check whether a path is staged
        bool contains(const std::string& path) const;

        // All staged entries
        const std::vector<IndexEntry>& entries() const { return entries_; }

        // Clear all staged entries
        void clear();

        // Persist the index to disk
        void write() const;

        // Reload the index from disk
        void read();

    private:
        fs::path index_path_;
        std::vector<IndexEntry> entries_;
        // Fast lookup: relative path -> index into entries_
        std::unordered_map<std::string, std::size_t> path_map_;

        void rebuildMap();

        // Parse binary index (version 1).  Throws on error.
        void readBinary(const std::vector<std::byte>& data);

        // Parse legacy plain-text index (backward compat).
        void readLegacyText(const std::string& text);
    };

} // namespace mygit
