#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"
#include "mygit/utils/gitignore.h"

#include <iostream>
#include <stdexcept>

namespace mygit {
    namespace cli {

        CommandResult runAdd(const Args& args) {
            if (args.empty()) {
                std::cerr << "usage: mygit add <file> [<file> ...]\n";
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

            utils::GitIgnore gi(repo.workDir());

            int errors = 0;
            for (const auto& file : args) {
                try {
                    const fs::path abs_path = fs::absolute(fs::path(file));
                    const fs::path rel_path = fs::relative(abs_path, repo.workDir());
                    if (gi.isIgnored(rel_path.string())) {
                        std::cerr << "warning: '" << file << "' is ignored by .gitignore, skipping\n";
                        continue;
                    }
                    repo.stageFile(abs_path);
                    std::cout << "staged: " << file << "\n";
                } catch (const std::exception& e) {
                    std::cerr << "error: " << e.what() << "\n";
                    ++errors;
                }
            }

            return {errors > 0 ? 1 : 0};
        }

    } // namespace cli
} // namespace mygit
