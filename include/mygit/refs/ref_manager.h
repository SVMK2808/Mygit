#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace fs = std::filesystem;

namespace mygit {

    // Manages Git references: branches, tags, and HEAD
    class RefManager {
    public:
        explicit RefManager(const fs::path& git_dir);

        // --- HEAD ---

        // Read the symbolic target of HEAD (e.g. "refs/heads/main")
        std::string readHead() const;

        // Update HEAD to point at a symbolic ref (detached or branch)
        void setHead(const std::string& ref);

        // Resolve HEAD to its commit hash (empty if unborn)
        std::optional<std::string> resolveHead() const;

        // --- Generic ref resolution ---

        // Resolve any ref name to a commit hash (follows symbolic refs)
        std::optional<std::string> resolve(const std::string& ref) const;

        // Write a hash directly to a ref (e.g. after a commit)
        void updateRef(const std::string& ref, const std::string& hash);

        // Delete a ref
        bool deleteRef(const std::string& ref);

        // --- Branches ---

        // Return the short name of the current branch (empty if detached)
        std::optional<std::string> currentBranch() const;

        // List all local branch names
        std::vector<std::string> listBranches() const;

        // Create a new branch pointing at the given commit hash
        void createBranch(const std::string& name, const std::string& hash);

        // Delete a branch by name
        bool deleteBranch(const std::string& name);

    private:
        fs::path git_dir_;

        // Convert a short branch name to its full ref path on disk
        fs::path branchPath(const std::string& name) const;

        // Read raw ref text from a file, following symbolic refs recursively.
        // depth prevents infinite loops from circular symbolic refs.
        std::optional<std::string> readRef(const fs::path& ref_path, int depth = 0) const;

        // Look up a fully-qualified refname in packed-refs
        std::optional<std::string> resolvePackedRef(const std::string& refname) const;

        // Write raw hash text to a file
        void writeRef(const fs::path& ref_path, const std::string& hash) const;
    };

} // namespace mygit
