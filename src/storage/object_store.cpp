#include "mygit/storage/object_store.h"
#include "mygit/storage/file_utils.h"
#include "mygit/utils/compression.h"

#include <stdexcept>

namespace mygit {

    ObjectStore::ObjectStore(const fs::path& objects_dir)
        : objects_dir_(objects_dir) {
        storage::FileUtils::ensureDirectory(objects_dir_);
    }

    std::string ObjectStore::store(const Object& obj) {
        const std::string hash = obj.hash();
        if (exists(hash)) return hash; // already stored

        // Build full object bytes: "<type> <size>\0<raw_content>"
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

    std::optional<std::vector<std::byte>> ObjectStore::load(const std::string& hash) const {
        const fs::path path = getObjectPath(hash);
        if (!fs::exists(path)) return std::nullopt;

        const auto compressed = storage::FileUtils::readFile(path);
        const auto full = utils::Compression::decompress(compressed);

        // Skip the "<type> <size>\0" header
        for (size_t i = 0; i < full.size(); ++i) {
            if (full[i] == static_cast<std::byte>('\0')) {
                return std::vector<std::byte>(full.begin() + static_cast<ptrdiff_t>(i) + 1, full.end());
            }
        }

        return std::nullopt; // malformed — no null byte found
    }

    bool ObjectStore::exists(const std::string& hash) const {
        return fs::exists(getObjectPath(hash));
    }

    fs::path ObjectStore::getObjectPath(const std::string& hash) const {
        auto [dir, file] = splitHash(hash);
        return objects_dir_ / dir / file;
    }

    std::pair<std::string, std::string> ObjectStore::splitHash(const std::string& hash) const {
        if (hash.size() < 4) {
            throw std::invalid_argument("Hash too short: " + hash);
        }
        return { hash.substr(0, 2), hash.substr(2) };
    }

} // namespace mygit
