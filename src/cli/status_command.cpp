#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"
#include "mygit/objects/blob.h"
#include "mygit/storage/file_utils.h"
#include "mygit/utils/gitignore.h"

#include <iostream>
#include <set>

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

            const auto& entries = repo.index().entries();

            // ── Changes staged for commit ──────────────────────────────────
            std::cout << "Changes staged for commit:\n";
            if (entries.empty()) {
                std::cout << "  (nothing to commit)\n";
            } else {
                for (const auto& e : entries) {
                    std::cout << "  new file:   " << e.path << "\n";
                }
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
                    // Re-hash to detect content modifications
                    try {
                        const Blob current(storage::FileUtils::readFile(full));
                        if (current.hash() != e.hash) {
                            std::cout << "  modified:   " << e.path << "\n";
                            any_modified = true;
                        }
                    } catch (...) {}
                }
            }
            if (!any_modified) {
                std::cout << "  (none)\n";
            }

            // ── Untracked files ───────────────────────────────────────────
            std::set<std::string> staged_paths;
            for (const auto& e : entries) staged_paths.insert(e.path);

            utils::GitIgnore gi(repo.workDir());

            std::cout << "\nUntracked files:\n";
            bool any_untracked = false;
            std::error_code ec;
            for (const auto& de : fs::recursive_directory_iterator(repo.workDir(), ec)) {
                if (!de.is_regular_file()) continue;
                const std::string rel = fs::relative(de.path(), repo.workDir()).string();
                // Skip .git directory
                if (rel.rfind(".git", 0) == 0) continue;
                if (gi.isIgnored(rel)) continue;
                if (staged_paths.find(rel) == staged_paths.end()) {
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
