#include "mygit/refs/ref_manager.h"
#include "mygit/storage/file_utils.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace mygit {

    RefManager::RefManager(const fs::path& git_dir)
        : git_dir_(git_dir) {}

    // --- HEAD ---

    std::string RefManager::readHead() const {
        const fs::path head_path = git_dir_ / "HEAD";
        if (!fs::exists(head_path)) return "";
        std::string content = storage::FileUtils::readText(head_path);
        // Trim trailing newline/whitespace
        while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' ')) {
            content.pop_back();
        }
        return content;
    }

    void RefManager::setHead(const std::string& ref) {
        storage::FileUtils::atomicWriteText(git_dir_ / "HEAD", "ref: " + ref + "\n");
    }

    std::optional<std::string> RefManager::resolveHead() const {
        return resolve("HEAD");
    }

    // --- Generic ref resolution ---

    std::optional<std::string> RefManager::resolve(const std::string& ref) const {
        // Support symbolic names: HEAD → refs/heads/main → <hash>
        fs::path ref_path;
        if (ref == "HEAD") {
            ref_path = git_dir_ / "HEAD";
        } else if (ref.rfind("refs/", 0) == 0) {
            ref_path = git_dir_ / ref;
        } else {
            // Try loose ref first: refs/heads/<ref>
            fs::path heads_path = git_dir_ / "refs" / "heads" / ref;
            if (fs::exists(heads_path)) {
                ref_path = heads_path;
            } else {
                // Check packed-refs for this name
                const std::string full_ref = "refs/heads/" + ref;
                auto packed = resolvePackedRef(full_ref);
                if (packed) return packed;
                return std::nullopt;
            }
        }
        return readRef(ref_path);
    }

    void RefManager::updateRef(const std::string& ref, const std::string& hash) {
        writeRef(git_dir_ / ref, hash);
    }

    bool RefManager::deleteRef(const std::string& ref) {
        fs::path ref_path = git_dir_ / ref;
        if (!fs::exists(ref_path)) return false;
        fs::remove(ref_path);
        return true;
    }

    // --- Branches ---

    std::optional<std::string> RefManager::currentBranch() const {
        const std::string head = readHead();
        const std::string prefix = "ref: refs/heads/";
        if (head.rfind(prefix, 0) == 0) {
            return head.substr(prefix.size());
        }
        return std::nullopt; // detached HEAD
    }

    std::vector<std::string> RefManager::listBranches() const {
        const fs::path heads_dir = git_dir_ / "refs" / "heads";
        std::vector<std::string> branches;

        // Loose refs
        if (fs::exists(heads_dir)) {
            for (const auto& entry : fs::directory_iterator(heads_dir)) {
                if (entry.is_regular_file()) {
                    branches.push_back(entry.path().filename().string());
                }
            }
        }

        // packed-refs — add names not already found as loose refs
        const fs::path packed = git_dir_ / "packed-refs";
        if (fs::exists(packed)) {
            try {
                std::istringstream iss(storage::FileUtils::readText(packed));
                std::string line;
                while (std::getline(iss, line)) {
                    if (line.empty() || line[0] == '#' || line[0] == '^') continue;
                    // Format: "<hash> <refname>"
                    const size_t sp = line.find(' ');
                    if (sp == std::string::npos) continue;
                    const std::string refname = line.substr(sp + 1);
                    const std::string prefix  = "refs/heads/";
                    if (refname.rfind(prefix, 0) != 0) continue;
                    const std::string bname = refname.substr(prefix.size());
                    if (std::find(branches.begin(), branches.end(), bname) == branches.end()) {
                        branches.push_back(bname);
                    }
                }
            } catch (...) {}
        }

        std::sort(branches.begin(), branches.end());
        return branches;
    }

    void RefManager::createBranch(const std::string& name, const std::string& hash) {
        writeRef(branchPath(name), hash);
    }

    bool RefManager::deleteBranch(const std::string& name) {
        const fs::path p = branchPath(name);
        if (!fs::exists(p)) return false;
        fs::remove(p);
        return true;
    }

    // --- Private helpers ---

    fs::path RefManager::branchPath(const std::string& name) const {
        return git_dir_ / "refs" / "heads" / name;
    }

    std::optional<std::string> RefManager::readRef(const fs::path& ref_path) const {
        if (!fs::exists(ref_path)) {
            // Fall back to packed-refs before giving up
            // Reconstruct the ref name relative to git_dir_
            std::error_code ec;
            const auto rel = fs::relative(ref_path, git_dir_, ec);
            if (!ec) {
                auto packed = resolvePackedRef(rel.string());
                if (packed) return packed;
            }
            return std::nullopt;
        }

        std::string content = storage::FileUtils::readText(ref_path);
        // Trim whitespace
        while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' ')) {
            content.pop_back();
        }

        // Symbolic ref: "ref: refs/heads/main"
        if (content.rfind("ref: ", 0) == 0) {
            const std::string target = content.substr(5);
            return readRef(git_dir_ / target);
        }

        // Raw hash (40 hex chars): unborn if empty
        if (content.empty()) return std::nullopt;
        return content;
    }

    void RefManager::writeRef(const fs::path& ref_path, const std::string& hash) const {
        storage::FileUtils::atomicWriteText(ref_path, hash + "\n");
    }

    std::optional<std::string> RefManager::resolvePackedRef(const std::string& refname) const {
        const fs::path packed = git_dir_ / "packed-refs";
        if (!fs::exists(packed)) return std::nullopt;
        try {
            std::istringstream iss(storage::FileUtils::readText(packed));
            std::string line;
            while (std::getline(iss, line)) {
                if (line.empty() || line[0] == '#' || line[0] == '^') continue;
                const size_t sp = line.find(' ');
                if (sp == std::string::npos) continue;
                if (line.substr(sp + 1) == refname) {
                    return line.substr(0, sp);
                }
            }
        } catch (...) {}
        return std::nullopt;
    }

} // namespace mygit
