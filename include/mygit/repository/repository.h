#pragma once

#include "mygit/storage/object_store.h"
#include "mygit/index/index.h"
#include "mygit/refs/ref_manager.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace mygit {
    // Main repository class
    class Repository {
    public:
        // Initialise new repository
        static Repository init(const fs::path& path);

        //Open existing repository
        static std::optional<Repository> open(const fs::path& path);

        //Find repository root from current directory
        static std::optional<fs::path> findRoot(const fs::path& start_path);
        
        // Accessors
        const fs::path& workDir() const { return workDir_; }
        const fs::path& gitDir() const { return gitDir_; }
        ObjectStore& objectStore() { return *object_store_; }
        const ObjectStore& objectStore() const { return *object_store_; }
        Index& index() { return *index_; }
        RefManager& refs() { return *ref_manager_; }
        const RefManager& refs() const { return *ref_manager_; }

        //High-level operations
        void stageFile(const fs::path& file_path);
        std::string createCommit(const std::string& message,
                                 const std::string& author_name,
                                 const std::string& author_email);

        // Read a key from .git/config; returns "" if not found
        std::string readConfig(const std::string& section,
                               const std::string& key) const;

        // Recursively flatten a tree object into a {relative_path -> blob_hash} map.
        // Sub-tree entries are followed recursively; only blobs are returned.
        std::unordered_map<std::string, std::string>
        flattenTree(const std::string& tree_hash, const std::string& prefix = "") const;

        // Return flat {path -> blob_hash} map for the HEAD commit's tree.
        // Returns empty map if HEAD is unborn or the tree cannot be loaded.
        std::unordered_map<std::string, std::string> getCommittedFiles() const;

        // Returns true if any indexed file has been modified or deleted in the
        // working tree since it was staged.  The paths of dirty files are
        // appended to `dirty_paths`.
        bool hasDirtyWorkingTree(std::vector<std::string>& dirty_paths) const;
                   
    private:
        Repository(const fs::path& workDir, const fs::path& gitDir);

        fs::path workDir_;
        fs::path gitDir_;
        std::unique_ptr<ObjectStore> object_store_;
        std::unique_ptr<Index> index_;
        std::unique_ptr<RefManager> ref_manager_;
    };
}
