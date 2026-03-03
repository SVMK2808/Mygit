#include "mygit/objects/blob.h"
#include "mygit/utils/hash.h"
#include "mygit/storage/file_utils.h"

#include <fstream>
#include <stdexcept>

namespace mygit {

    Blob::Blob(std::vector<std::byte> content)
        : content_(std::move(content)) {}

    Blob::Blob(const std::string& content) {
        content_.reserve(content.size());
        for (unsigned char c : content) {
            content_.push_back(static_cast<std::byte>(c));
        }
    }

    Blob Blob::fromFile(const std::string& filePath) {
        return Blob(storage::FileUtils::readFile(fs::path(filePath)));
    }

    std::string Blob::hash() const {
        if (cached_hash_.empty()) {
            cached_hash_ = utils::Hash::objectHash("blob", content_);
        }
        return cached_hash_;
    }

    std::vector<std::byte> Blob::serialize() const {
        // Raw content — ObjectStore wraps it in the "<type> <size>\0" header
        return content_;
    }

    std::string Blob::contentAsString() const {
        std::string result;
        result.reserve(content_.size());
        for (std::byte b : content_) {
            result.push_back(static_cast<char>(b));
        }
        return result;
    }

    Blob Blob::deserialize(const std::vector<std::byte>& data) {
        // The stored data is the raw content (no header at this layer)
        return Blob(data);
    }

} // namespace mygit
