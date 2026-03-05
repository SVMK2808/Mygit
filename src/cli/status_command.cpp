#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"
#include "mygit/objects/blob.h"
#include "mygit/storage/file_utils.h"
#include "mygit/utils/gitignore.h"

#include <iostream>
#include <set>
#include <unordered_map>

namespace mygit {
    namespace cli {

        CommandResult runStatus(const Args& /*args*/) {
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

            // Show current branch
            if (auto branch = repo.refs().currentBranch()) {
                std::cout << "On branch " << *branch << "\n";
            } else {
                std::cout << "HEAD detached\n";
            }

            const auto& entries = repo.index().entries();

            // Get the flat {path -> hash} map for the last commit
            const auto committed = repo.getCommittedFiles();

            // ── Changes staged for commit ──────────────────────────────────
            std::cout << "\nChanges staged for commit:\n";
            bool any_staged = false;
            for (const auto& e : entries) {
                auto it = committed.find(e.path);
                if (it == committed.end()) {
                    std::cout << "  new file:   " << e.path << "\n";
                    any_staged = true;
                } else if (it->second != e.hash) {
                    std::cout << "  modified:   " << e.path << "\n";
                    any_staged = true;
                }
                // If hashes match, the file is unchanged relative to HEAD (not shown)
            }
            // Also show staged deletions: committed files no longer in the index
            std::set<std::string> indexed_paths;
            for (const auto& e : entries) indexed_paths.insert(e.path);
            for (const auto& [path, _] : committed) {
                if (indexed_paths.find(path) == indexed_paths.end()) {
                    std::cout << "  deleted:    " << path << "\n";
                    any_staged = true;
                }
            }
            if (!any_staged) {
                std::cout << "  (nothing staged)\n";
            }

            // ── Changes not staged for commit ─────────────────────────────
            std::cout << "\nChanges not staged for commit:\n";
            bool any_modified = false;
            for (const auto& e : entries) {
                const fs::path full = repo.workDir() / e.path;
                if (!fs::exists(full)) {
                    std::cout << "  deleted:    " << e.path << "\n";
                    any_modified = true;
                } else {
                    // Use size as a quick pre-filter before rehashing
                    std::error_code ec;
                    const auto sz = fs::file_size(full, ec);
                    bool changed = (!ec && sz != e.file_size);
                    if (!changed) {
                        try {
                            const Blob current(storage::FileUtils::readFile(full));
                            changed = (current.hash() != e.hash);
                        } catch (...) {
                            changed = true;
                        }
                    }
                    if (changed) {
                        std::cout << "  modified:   " << e.path << "\n";
                        any_modified = true;
                    }
                }
            }
            if (!any_modified) {
                std::cout << "  (none)\n";
            }

            // ── Untracked files ───────────────────────────────────────────
            utils::GitIgnore gi(repo.workDir());

            std::cout << "\nUntracked files:\n";
            bool any_untracked = false;
            std::error_code ec;
            for (const auto& de : fs::recursive_directory_iterator(repo.workDir(), ec)) {
                if (!de.is_regular_file()) continue;
                const std::string rel = fs::relative(de.path(), repo.workDir()).string();
                if (rel.rfind(".git", 0) == 0) continue;
                if (gi.isIgnored(rel)) continue;
                if (indexed_paths.find(rel) == indexed_paths.end()) {
                    std::cout << "  " << rel << "\n";
                    any_untracked = true;
                }
            }
            if (!any_untracked) {
                std::cout << "  (none)\n";
            }

            return {0};
        }

    } // namespace cli
} // namespace mygit
