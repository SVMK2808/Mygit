// rm_command.cpp – implementation of `mygit rm [--cached] <file>...`
//
// Default behaviour (without --cached):
//   • Remove the file from the staging index
//   • Delete the file from the working tree
//
// With --cached:
//   • Remove the file from the staging index only; leave the working-tree
//     copy untouched (useful for un-staging without losing local changes).
//
// Exit code: 0 on full success, 1 if any path was not found in the index
// or could not be processed.

#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mygit {
    namespace cli {

        CommandResult runRm(const Args& args) {
            if (args.empty()) {
                std::cerr << "usage: mygit rm [--cached] <file> [<file>...]\n"
                          << "\n"
                          << "    --cached    Remove from index only; keep working-tree file\n";
                return {1};
            }

            // ── locate the repository ──────────────────────────────────────
            auto root = Repository::findRoot(fs::current_path());
            if (!root) {
                std::cerr << "fatal: not a mygit repository (or any parent directory)\n";
                return {1};
            }
            auto repo_opt = Repository::open(*root);
            if (!repo_opt) {
                std::cerr << "fatal: could not open repository\n";
                return {1};
            }
            auto& repo = *repo_opt;

            // ── parse flags ────────────────────────────────────────────────
            bool cached_only = false;
            std::vector<std::string> files;

            for (const auto& a : args) {
                if (a == "--cached") {
                    cached_only = true;
                } else if (a == "--") {
                    // conventional end-of-options marker
                    continue;
                } else {
                    files.push_back(a);
                }
            }

            if (files.empty()) {
                std::cerr << "usage: mygit rm [--cached] <file> [<file>...]\n";
                return {1};
            }

            // ── process each file ──────────────────────────────────────────
            int errors = 0;
            bool index_dirty = false;

            for (const auto& file : files) {
                try {
                    std::error_code ec;
                    const fs::path abs = fs::absolute(fs::path(file));
                    const fs::path rel = fs::relative(abs, repo.workDir(), ec);

                    if (ec) {
                        std::cerr << "error: '" << file
                                  << "' is outside the repository\n";
                        ++errors;
                        continue;
                    }

                    // Normalise path separator to '/' for the index key
                    const std::string rel_str = rel.generic_string();

                    if (!repo.index().contains(rel_str)) {
                        std::cerr << "error: pathspec '" << file
                                  << "' did not match any files known to mygit\n";
                        ++errors;
                        continue;
                    }

                    repo.index().remove(rel_str);
                    index_dirty = true;

                    if (!cached_only) {
                        const fs::path wt_path = repo.workDir() / rel;
                        if (fs::exists(wt_path, ec)) {
                            fs::remove(wt_path, ec);
                            if (ec) {
                                std::cerr << "warning: could not delete '"
                                          << wt_path.string()
                                          << "': " << ec.message() << "\n";
                            }
                        }
                    }

                    std::cout << "rm '" << rel_str << "'\n";

                } catch (const std::exception& e) {
                    std::cerr << "error: " << e.what() << "\n";
                    ++errors;
                }
            }

            // Persist the index once for all successful removals
            if (index_dirty) {
                repo.index().write();
            }

            return {errors > 0 ? 1 : 0};
        }

    } // namespace cli
} // namespace mygit
