#include "mygit/objects/tree.h"
#include "mygit/utils/hash.h"

#include <sstream>
#include <stdexcept>

namespace mygit {

    void Tree::addEntry(const std::string& name, const std::string& hash,
                        FileMode mode, ObjectType type) {
        TreeEntry entry;
        entry.name = name;
        entry.hash = hash;
        entry.mode = mode;
        entry.type = type;
        entries_[name] = std::move(entry);
        cached_hash_.clear();
    }

    std::string Tree::hash() const {
        if (cached_hash_.empty()) {
            cached_hash_ = utils::Hash::objectHash("tree", serialize());
        }
        return cached_hash_;
    }

    // Serialise format (one line per entry, sorted by name via std::map):
    //   <octal_mode> <name>\0<40-char-hex-hash>
    // The null byte acts as a delimiter between name and hash (analogous to Git's binary format).
    std::vector<std::byte> Tree::serialize() const {
        std::vector<std::byte> out;

        for (const auto& [name, entry] : entries_) {
            // mode string
            std::string mode_str = std::to_string(static_cast<uint32_t>(entry.mode));

            // "<mode> <name>\0"
            for (char c : mode_str) out.push_back(static_cast<std::byte>(c));
            out.push_back(static_cast<std::byte>(' '));
            for (char c : name) out.push_back(static_cast<std::byte>(c));
            out.push_back(static_cast<std::byte>('\0'));

            // 40-char hex hash followed by newline for readability
            for (char c : entry.hash) out.push_back(static_cast<std::byte>(c));
            out.push_back(static_cast<std::byte>('\n'));
        }

        return out;
    }

    size_t Tree::size() const {
        return serialize().size();
    }

    bool Tree::hasEntry(const std::string& name) const {
        return entries_.count(name) > 0;
    }

    const TreeEntry* Tree::getEntry(const std::string& name) const {
        auto it = entries_.find(name);
        return (it != entries_.end()) ? &it->second : nullptr;
    }

    Tree Tree::deserialize(const std::vector<std::byte>& data) {
        Tree tree;

        size_t i = 0;
        while (i < data.size()) {
            // Read mode string up to ' '
            std::string mode_str;
            while (i < data.size() && data[i] != static_cast<std::byte>(' ')) {
                mode_str.push_back(static_cast<char>(data[i++]));
            }
            ++i; // skip ' '

            // Read name up to '\0'
            std::string name;
            while (i < data.size() && data[i] != static_cast<std::byte>('\0')) {
                name.push_back(static_cast<char>(data[i++]));
            }
            ++i; // skip '\0'

            // Read 40-char hex hash
            std::string hash_str;
            for (int k = 0; k < 40 && i < data.size(); ++k, ++i) {
                hash_str.push_back(static_cast<char>(data[i]));
            }

            // Skip trailing newline
            if (i < data.size() && data[i] == static_cast<std::byte>('\n')) ++i;

            uint32_t mode_val = static_cast<uint32_t>(std::stoul(mode_str));
            FileMode mode = static_cast<FileMode>(mode_val);
            ObjectType type = (mode == FileMode::Directory) ? ObjectType::Tree : ObjectType::Blob;

            tree.addEntry(name, hash_str, mode, type);
        }

        return tree;
    }

} // namespace mygit
