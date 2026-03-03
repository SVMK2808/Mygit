#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"

#include <iostream>

namespace mygit {
    namespace cli {

        CommandResult runBranch(const Args& args) {
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

            if (args.empty()) {
                // List branches
                const auto branches = repo.refs().listBranches();
                const auto current  = repo.refs().currentBranch();

                if (branches.empty()) {
                    std::cout << "(no branches yet)\n";
                    return {0};
                }
                for (const auto& b : branches) {
                    if (current && *current == b) {
                        std::cout << "* " << b << "\n";
                    } else {
                        std::cout << "  " << b << "\n";
                    }
                }
                return {0};
            }

            // Create a new branch
            const std::string& name = args[0];

            // The new branch should point to the current HEAD commit
            auto head_hash = repo.refs().resolveHead();
            if (!head_hash) {
                std::cerr << "error: cannot create branch on an unborn repository "
                             "(make at least one commit first)\n";
                return {1};
            }

            try {
                repo.refs().createBranch(name, *head_hash);
                std::cout << "Branch '" << name << "' created at " << head_hash->substr(0, 7) << "\n";
                return {0};
            } catch (const std::exception& e) {
                std::cerr << "error: " << e.what() << "\n";
                return {1, e.what()};
            }
        }

    } // namespace cli
} // namespace mygit
