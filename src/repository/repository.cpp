#include "mygit/repository/repository.h"
#include "mygit/storage/file_utils.h"
#include "mygit/objects/blob.h"
#include "mygit/objects/tree.h"
#include "mygit/objects/commit.h"

#include <sys/stat.h>
#include <ctime>
#include <map>
#include <sstream>
#include <stdexcept>

namespace mygit {

    // -----------------------------------------------------------------------
    // Private constructor — wires sub-systems together
    // -----------------------------------------------------------------------
    Repository::Repository(const fs::path& workDir, const fs::path& gitDir)
        : workDir_(workDir)
        , gitDir_(gitDir)
        , object_store_(std::make_unique<ObjectStore>(gitDir / "objects"))
        , index_(std::make_unique<Index>(gitDir / "index"))
        , ref_manager_(std::make_unique<RefManager>(gitDir)) {}

    // -----------------------------------------------------------------------
    // init — create a new repository inside `path`
    // -----------------------------------------------------------------------
    Repository Repository::init(const fs::path& path) {
        const fs::path git_dir = path / ".git";

        if (fs::exists(git_dir)) {
            throw std::runtime_error("Repository already exists at: " + path.string());
        }

        // Create directory layout
        storage::FileUtils::ensureDirectory(git_dir / "objects");
        storage::FileUtils::ensureDirectory(git_dir / "refs" / "heads");
        storage::FileUtils::ensureDirectory(git_dir / "refs" / "tags");

        // HEAD starts on 'main'
        storage::FileUtils::atomicWriteText(git_dir / "HEAD", "ref: refs/heads/main\n");

        // Minimal config
        storage::FileUtils::atomicWriteText(git_dir / "config",
            "[core]\n"
            "\trepositoryformatversion = 0\n"
            "\tfilemode = true\n"
            "\tbare = false\n");

        return Repository(path, git_dir);
    }

    // -----------------------------------------------------------------------
    // open — open an existing repository whose .git is directly inside `path`
    // -----------------------------------------------------------------------
    std::optional<Repository> Repository::open(const fs::path& path) {
        const fs::path git_dir = path / ".git";
        if (!fs::exists(git_dir) || !fs::is_directory(git_dir)) {
            return std::nullopt;
        }
        return Repository(path, git_dir);
    }

    // -----------------------------------------------------------------------
    // findRoot — walk upward from start_path until we find a .git directory
    // -----------------------------------------------------------------------
    std::optional<fs::path> Repository::findRoot(const fs::path& start_path) {
        fs::path current = fs::absolute(start_path);

        while (true) {
            if (fs::exists(current / ".git")) {
                return current;
            }
            const fs::path parent = current.parent_path();
            if (parent == current) break; // filesystem root
            current = parent;
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // stageFile — hash the file content, store the blob, update the index
    // -----------------------------------------------------------------------
    void Repository::stageFile(const fs::path& file_path) {
        const fs::path abs_path = fs::absolute(file_path);

        // Path traversal protection
        if (!storage::FileUtils::isUnder(abs_path, workDir_)) {
            throw std::runtime_error(
                "Path is outside the work directory: " + abs_path.string());
        }

        if (!fs::exists(abs_path)) {
            throw std::runtime_error("File not found: " + abs_path.string());
        }
        if (!fs::is_regular_file(abs_path)) {
            throw std::runtime_error("Not a regular file: " + abs_path.string());
        }

        Blob blob = Blob::fromFile(abs_path.string());
        const std::string hash = object_store_->store(blob);

        // Relative path from work directory is used as the index key
        const std::string rel_path = fs::relative(abs_path, workDir_).string();

        // Use POSIX stat() for reliable mtime (seconds since epoch)
        struct ::stat st{};
        const std::time_t mtime =
            (::stat(abs_path.c_str(), &st) == 0) ? st.st_mtime : std::time(nullptr);

        IndexEntry entry;
        entry.path      = rel_path;
        entry.hash      = hash;
        entry.mtime     = mtime;
        entry.file_size = static_cast<uint64_t>(fs::file_size(abs_path));

        index_->add(entry);
        index_->write();
    }

    // -----------------------------------------------------------------------
    // readConfig — parse .git/config for a key under a section
    //   Returns empty string if not found.
    // -----------------------------------------------------------------------
    std::string Repository::readConfig(const std::string& section,
                                       const std::string& key) const {
        const fs::path cfg = gitDir_ / "config";
        if (!fs::exists(cfg)) return {};
        try {
            std::istringstream iss(storage::FileUtils::readText(cfg));
            std::string line;
            bool in_section = false;
            while (std::getline(iss, line)) {
                // Trim leading whitespace
                size_t start = line.find_first_not_of(" \t");
                if (start == std::string::npos) continue;
                line = line.substr(start);

                if (line[0] == '[') {
                    // Section header: [section] or [section "subsection"]
                    const size_t close = line.find(']');
                    const std::string hdr = (close != std::string::npos)
                                            ? line.substr(1, close - 1)
                                            : line.substr(1);
                    // Strip subsection
                    const std::string sec = hdr.substr(0, hdr.find(' '));
                    in_section = (sec == section);
                    continue;
                }
                if (!in_section) continue;

                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string k = line.substr(0, eq);
                // Trim trailing whitespace from key
                while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
                if (k != key) continue;

                std::string v = line.substr(eq + 1);
                // Trim leading whitespace from value
                size_t vs = v.find_first_not_of(" \t");
                return (vs != std::string::npos) ? v.substr(vs) : "";
            }
        } catch (...) {}
        return {};
    }

    // -----------------------------------------------------------------------
    // buildRecursiveTree — build nested tree objects from flat index entries
    //   All entries in `entries` must have paths relative to the current level.
    // -----------------------------------------------------------------------
    static std::string buildRecursiveTree(ObjectStore& store,
                                          const std::vector<IndexEntry>& entries) {
        Tree tree;
        // Entries that are direct files at this level
        // Entries that belong to subdirectories, grouped by dir name
        std::map<std::string, std::vector<IndexEntry>> subdirs;

        for (const auto& e : entries) {
            const size_t slash = e.path.find('/');
            if (slash == std::string::npos) {
                // Direct file at this tree level
                tree.addEntry(e.path, e.hash, FileMode::Regular, ObjectType::Blob);
            } else {
                // Belongs to a subdirectory
                const std::string dir = e.path.substr(0, slash);
                IndexEntry sub = e;
                sub.path = e.path.substr(slash + 1); // strip leading "dir/"
                subdirs[dir].push_back(sub);
            }
        }

        // Recursively build subtrees
        for (auto& [dirname, sub_entries] : subdirs) {
            const std::string subtree_hash = buildRecursiveTree(store, sub_entries);
            tree.addEntry(dirname, subtree_hash, FileMode::Directory, ObjectType::Tree);
        }

        return store.store(tree);
    }

    // -----------------------------------------------------------------------
    // createCommit — build tree from index, write commit, advance HEAD
    // -----------------------------------------------------------------------
    std::string Repository::createCommit(const std::string& message,
                                         const std::string& author_name,
                                         const std::string& author_email) {
        if (index_->entries().empty()) {
            throw std::runtime_error("Nothing to commit — staging area is empty.");
        }

        // Resolve author from config if caller passed empty strings
        std::string a_name  = author_name.empty()  ? readConfig("user", "name")  : author_name;
        std::string a_email = author_email.empty() ? readConfig("user", "email") : author_email;
        if (a_name.empty())  a_name  = "Unknown Author";
        if (a_email.empty()) a_email = "unknown@example.com";

        // Build recursive tree from every staged entry
        const std::string tree_hash =
            buildRecursiveTree(*object_store_, index_->entries());

        // Collect parent hashes from HEAD
        std::vector<std::string> parents;
        if (auto head_hash = ref_manager_->resolveHead()) {
            parents.push_back(*head_hash);
        }

        CommitMetadata meta;
        meta.author_name  = a_name;
        meta.author_email = a_email;
        meta.timestamp    = std::time(nullptr);
        meta.message      = message;

        Commit commit(tree_hash, parents, meta);
        const std::string commit_hash = object_store_->store(commit);

        // Advance the current branch (or write hash to HEAD if detached)
        if (auto branch = ref_manager_->currentBranch()) {
            ref_manager_->updateRef("refs/heads/" + *branch, commit_hash);
        } else {
            storage::FileUtils::atomicWriteText(gitDir_ / "HEAD", commit_hash + "\n");
        }

        return commit_hash;
    }

} // namespace mygit
