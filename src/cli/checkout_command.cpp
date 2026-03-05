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

        // Validate a branch name against path traversal and illegal characters
        static bool isValidCheckoutName(const std::string& name, std::string& err) {
            if (name.empty()) { err = "branch name must not be empty"; return false; }
            if (name[0] == '-') { err = "branch name must not start with '-'"; return false; }
            for (size_t i = 0; i < name.size(); ++i) {
                const char c = name[i];
                if (c == '\0' || c == '\n' || c == '\r' || c == '\\') {
                    err = "branch name contains illegal character"; return false;
                }
                if (c == '.' && i + 1 < name.size() && name[i+1] == '.') {
                    err = "branch name must not contain '..'"; return false;
                }
            }
            if (name.front() == '/' || name.back() == '/') {
                err = "branch name must not start or end with '/'"; return false;
            }
            return true;
        }

        // Recursively restore all blob files from tree_hash to dest_root.
        // prefix is the relative path component already consumed.
        // Returns the set of relative file paths written.
        static std::set<std::string> restoreTree(
                Repository& repo,
                const std::string& tree_hash,
                const fs::path& dest_root,
                const std::string& prefix = "")
        {
            std::set<std::string> written;
            if (tree_hash.empty()) return written;

            auto tree_raw = repo.objectStore().load(tree_hash);
            if (!tree_raw) return written;

            Tree tree;
            try { tree = Tree::deserialize(*tree_raw); }
            catch (...) { return written; }

            for (const auto& [name, entry] : tree.entries()) {
                const std::string rel = prefix.empty() ? name : prefix + "/" + name;

                if (entry.type == ObjectType::Blob) {
                    auto blob_raw = repo.objectStore().load(entry.hash);
                    if (!blob_raw) {
                        std::cerr << "warning: blob " << entry.hash.substr(0, 7)
                                  << " for '" << rel << "' not found — skipping\n";
                        continue;
                    }
                    const fs::path dest = dest_root / rel;
                    storage::FileUtils::ensureDirectory(dest.parent_path());
                    storage::FileUtils::writeFile(dest, *blob_raw);
                    written.insert(rel);

                } else if (entry.type == ObjectType::Tree) {
                    auto sub = restoreTree(repo, entry.hash, dest_root, rel);
                    written.insert(sub.begin(), sub.end());
                }
            }
            return written;
        }

        CommandResult runCheckout(const Args& args) {
            if (args.empty()) {
                std::cerr << "usage: mygit checkout <branch>\n";
                return {1};
            }

            const std::string& branch_name = args[0];

            // Reject malicious or malformed branch names before any I/O
            std::string name_err;
            if (!isValidCheckoutName(branch_name, name_err)) {
                std::cerr << "error: invalid branch name '"
                          << branch_name << "': " << name_err << "\n";
                return {1};
            }

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

            // ── Dirty-tree guard: refuse to proceed if working tree has changes ──
            std::vector<std::string> dirty;
            if (repo.hasDirtyWorkingTree(dirty)) {
                std::cerr << "error: your local changes would be overwritten by checkout:\n";
                for (const auto& d : dirty) {
                    std::cerr << "        " << d << "\n";
                }
                std::cerr << "Please commit or stash your changes before switching branches.\n";
                return {1};
            }

            // Guard: already on this branch?
            if (auto cur = repo.refs().currentBranch()) {
                if (*cur == branch_name) {
                    std::cout << "Already on '" << branch_name << "'\n";
                    return {0};
                }
            }

            // ── Collect files in the currently-checked-out tree (recursive) ──
            std::set<std::string> old_files;
            if (auto old_hash = repo.refs().resolveHead()) {
                if (auto old_raw = repo.objectStore().load(*old_hash)) {
                    try {
                        const Commit old_commit = Commit::deserialize(*old_raw);
                        const auto old_map = repo.flattenTree(old_commit.treeHash());
                        for (const auto& [p, _] : old_map) old_files.insert(p);
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

            Commit target_commit("", {}, CommitMetadata{});
            try {
                target_commit = Commit::deserialize(*commit_raw);
            } catch (const std::exception& e) {
                std::cerr << "error: cannot deserialize commit: " << e.what() << "\n";
                return {1};
            }

            // ── Restore files from the new tree (fully recursive) ─────────
            const std::set<std::string> new_files =
                restoreTree(repo, target_commit.treeHash(), repo.workDir());

            // ── Delete files that existed in old tree but not in new tree ─
            for (const auto& old_name : old_files) {
                if (new_files.find(old_name) == new_files.end()) {
                    const fs::path victim = repo.workDir() / old_name;
                    std::error_code ec;
                    fs::remove(victim, ec);
                    // Clean up now-empty parent directories
                    if (!ec) {
                        fs::path parent = victim.parent_path();
                        while (parent != repo.workDir()) {
                            std::error_code ec2;
                            if (fs::is_empty(parent, ec2) && !ec2) {
                                fs::remove(parent, ec2);
                            } else {
                                break;
                            }
                            parent = parent.parent_path();
                        }
                    }
                }
            }

            // Update HEAD to point at the branch
            repo.refs().setHead("refs/heads/" + branch_name);

            // ── Rebuild index to match new tree (recursive) ───────────────
            repo.index().clear();
            const auto new_committed = repo.flattenTree(target_commit.treeHash());
            for (const auto& [rel_path, blob_hash] : new_committed) {
                const fs::path dest = repo.workDir() / rel_path;
                IndexEntry ie;
                ie.path      = rel_path;
                ie.hash      = blob_hash;
                struct ::stat st{};
                ie.mtime     = (fs::exists(dest) && ::stat(dest.c_str(), &st) == 0)
                               ? st.st_mtime : 0;
                std::error_code ec;
                ie.file_size = fs::exists(dest) ? fs::file_size(dest, ec) : 0;
                repo.index().add(ie);
            }
            repo.index().write();

            std::cout << "Switched to branch '" << branch_name << "'\n";
            return {0};
        }

    } // namespace cli
} // namespace mygit
