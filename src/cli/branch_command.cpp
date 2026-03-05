#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"

#include <iostream>

namespace mygit {
    namespace cli {

        // Validate a branch name: reject characters/patterns that could cause
        // path traversal or break ref-file naming conventions.
        static bool isValidBranchName(const std::string& name, std::string& err) {
            if (name.empty()) {
                err = "branch name must not be empty";
                return false;
            }
            if (name[0] == '-') {
                err = "branch name must not start with '-'";
                return false;
            }
            // Reject path-traversal sequences and control characters
            for (size_t i = 0; i < name.size(); ++i) {
                const char c = name[i];
                if (c == '\0' || c == '\n' || c == '\r' || c == '\\') {
                    err = "branch name contains illegal character";
                    return false;
                }
                if (c == '.' && i + 1 < name.size() && name[i+1] == '.') {
                    err = "branch name must not contain '..'";
                    return false;
                }
            }
            // Must not start or end with '/'
            if (name.front() == '/' || name.back() == '/') {
                err = "branch name must not start or end with '/'";
                return false;
            }
            // Must not contain consecutive slashes
            if (name.find("//") != std::string::npos) {
                err = "branch name must not contain '//'";
                return false;
            }
            return true;
        }

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

            // Validate the branch name
            std::string name_err;
            if (!isValidBranchName(name, name_err)) {
                std::cerr << "error: invalid branch name '" << name << "': " << name_err << "\n";
                return {1};
            }

            // The new branch should point to the current HEAD commit
            auto head_hash = repo.refs().resolveHead();
            if (!head_hash) {
                std::cerr << "error: cannot create branch on an unborn repository "
                             "(make at least one commit first)\n";
                return {1};
            }

            // Refuse to silently overwrite an existing branch
            if (repo.refs().resolve(name)) {
                std::cerr << "error: branch '" << name << "' already exists\n";
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
