#include "mygit/index/index.h"
#include "mygit/storage/file_utils.h"

#include <sstream>
#include <stdexcept>

namespace mygit {

    Index::Index(const fs::path& index_path)
        : index_path_(index_path) {
        if (fs::exists(index_path_)) {
            read();
        }
    }

    void Index::add(const IndexEntry& entry) {
        auto it = path_map_.find(entry.path);
        if (it != path_map_.end()) {
            entries_[it->second] = entry; // update existing
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

    // Format (plain text):
    //   <count>
    //   <hash> <mtime> <file_size> <path>
    //   ...
    void Index::write() const {
        std::ostringstream oss;
        oss << entries_.size() << "\n";
        for (const auto& e : entries_) {
            oss << e.hash << " "
                << static_cast<long long>(e.mtime) << " "
                << e.file_size << " "
                << e.path << "\n";
        }
        storage::FileUtils::atomicWriteText(index_path_, oss.str());
    }

    void Index::read() {
        const std::string text = storage::FileUtils::readText(index_path_);
        std::istringstream iss(text);

        size_t count = 0;
        iss >> count;
        iss.ignore(); // newline after count

        entries_.clear();
        for (size_t i = 0; i < count; ++i) {
            std::string line;
            if (!std::getline(iss, line)) break;

            std::istringstream ls(line);
            IndexEntry e;
            long long mtime_raw = 0;
            ls >> e.hash >> mtime_raw >> e.file_size;
            e.mtime = static_cast<std::time_t>(mtime_raw);

            // Remaining token is the path (may contain spaces in theory, but
            // we skip one space then take the rest of the line)
            ls.ignore(1);
            std::getline(ls, e.path);
            entries_.push_back(std::move(e));
        }
        rebuildMap();
    }

    void Index::rebuildMap() {
        path_map_.clear();
        for (size_t i = 0; i < entries_.size(); ++i) {
            path_map_[entries_[i].path] = i;
        }
    }

} // namespace mygit
