#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"
#include "mygit/objects/commit.h"
#include "mygit/objects/tree.h"
#include "mygit/storage/file_utils.h"

#include <sys/stat.h>
#include <iostream>
#include <set>
#include <stdexcept>

namespace mygit {
    namespace cli {

        CommandResult runCheckout(const Args& args) {
            if (args.empty()) {
                std::cerr << "usage: mygit checkout <branch>\n";
                return {1};
            }

            const std::string& branch_name = args[0];

            auto root = Repository::findRoot(fs::current_path());
            if (!root) {
                std::cerr << "fatal: not a mygit repository\n";
                return {1};
            }
            auto repo_opt = Repository::open(*root);
            if (!repo_opt) {
                std::cerr << "fatal: could not open repository\n";
                return {1};
            }
            auto& repo = *repo_opt;

            // ── Collect files in the currently-checked-out tree ───────────
            std::set<std::string> old_files;
            if (auto old_hash = repo.refs().resolveHead()) {
                if (auto old_raw = repo.objectStore().load(*old_hash)) {
                    try {
                        const Commit old_commit = Commit::deserialize(*old_raw);
                        if (auto old_tree_raw =
                                repo.objectStore().load(old_commit.treeHash())) {
                            const Tree old_tree = Tree::deserialize(*old_tree_raw);
                            for (const auto& [name, e] : old_tree.entries()) {
                                if (e.type == ObjectType::Blob)
                                    old_files.insert(name);
                            }
                        }
                    } catch (...) {}
                }
            }

            // Resolve target branch to a commit hash
            auto commit_hash_opt = repo.refs().resolve(branch_name);
            if (!commit_hash_opt) {
                std::cerr << "error: branch '" << branch_name << "' not found\n";
                return {1};
            }
            const std::string commit_hash = *commit_hash_opt;

            // Load and deserialize the commit
            auto commit_raw = repo.objectStore().load(commit_hash);
            if (!commit_raw) {
                std::cerr << "error: commit object " << commit_hash << " not found\n";
                return {1};
            }
            const Commit commit = Commit::deserialize(*commit_raw);

            // Load the tree
            auto tree_raw = repo.objectStore().load(commit.treeHash());
            if (!tree_raw) {
                std::cerr << "error: tree object " << commit.treeHash() << " not found\n";
                return {1};
            }
            const Tree tree = Tree::deserialize(*tree_raw);

            // ── Restore files from the new tree ───────────────────────────
            std::set<std::string> new_files;
            for (const auto& [name, entry] : tree.entries()) {
                if (entry.type != ObjectType::Blob) continue;
                new_files.insert(name);

                auto blob_raw = repo.objectStore().load(entry.hash);
                if (!blob_raw) continue;

                const fs::path dest = repo.workDir() / name;
                storage::FileUtils::ensureDirectory(dest.parent_path());
                storage::FileUtils::writeFile(dest, *blob_raw);
            }

            // ── Delete files that existed in old tree but not in new tree ─
            for (const auto& old_name : old_files) {
                if (new_files.find(old_name) == new_files.end()) {
                    const fs::path victim = repo.workDir() / old_name;
                    std::error_code ec;
                    fs::remove(victim, ec); // ignore errors (already gone etc.)
                }
            }

            // Update HEAD to point at the branch
            repo.refs().setHead("refs/heads/" + branch_name);

            // ── Rebuild index to match new tree ───────────────────────────
            repo.index().clear();
            for (const auto& [name, entry] : tree.entries()) {
                if (entry.type != ObjectType::Blob) continue;
                const fs::path dest = repo.workDir() / name;
                IndexEntry ie;
                ie.path      = name;
                ie.hash      = entry.hash;
                // Use POSIX stat() for reliable mtime
                struct ::stat st{};
                ie.mtime     = (fs::exists(dest) && ::stat(dest.c_str(), &st) == 0)
                               ? st.st_mtime : 0;
                ie.file_size = fs::exists(dest) ? fs::file_size(dest) : 0;
                repo.index().add(ie);
            }
            repo.index().write();

            std::cout << "Switched to branch '" << branch_name << "'\n";
            return {0};
        }

    } // namespace cli
} // namespace mygit
