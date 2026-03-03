#pragma once

#include "mygit/storage/object_store.h"
#include "mygit/index/index.h"
#include "mygit/refs/ref_manager.h"
#include <filesystem>
#include <memory>
#include <optional>

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
        Index& index() { return *index_; }
        RefManager& refs() { return *ref_manager_; }

        //High-level operations
        void stageFile(const fs::path& file_path);
        std::string createCommit(const std::string& message,
                                 const std::string& author_name,
                                 const std::string& author_email);

        // Read a key from .git/config; returns "" if not found
        std::string readConfig(const std::string& section,
                               const std::string& key) const;
                   
    private:
        Repository(const fs::path& workDir, const fs::path& gitDir);

        fs::path workDir_;
        fs::path gitDir_;
        std::unique_ptr<ObjectStore> object_store_;
        std::unique_ptr<Index> index_;
        std::unique_ptr<RefManager> ref_manager_;
    };
}
