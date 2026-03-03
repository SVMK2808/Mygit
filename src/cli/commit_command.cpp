#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"

#include <iostream>
#include <stdexcept>

namespace mygit {
    namespace cli {

        CommandResult runCommit(const Args& args) {
            // Parse: commit -m "message"
            std::string message;
            for (size_t i = 0; i < args.size(); ++i) {
                if ((args[i] == "-m" || args[i] == "--message") && i + 1 < args.size()) {
                    message = args[i + 1];
                    ++i;
                }
            }

            if (message.empty()) {
                std::cerr << "error: commit message is required (-m <message>)\n";
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

            try {
                // Prefer GIT_AUTHOR_* env vars; pass empty strings so createCommit
                // falls back to .git/config [user] name/email.
                const char* name_env  = std::getenv("GIT_AUTHOR_NAME");
                const char* email_env = std::getenv("GIT_AUTHOR_EMAIL");
                std::string hash = repo.createCommit(
                    message,
                    name_env  ? name_env  : "",
                    email_env ? email_env : ""
                );
                std::cout << "[" << hash.substr(0, 7) << "] " << message << "\n";
                return {0};
            } catch (const std::exception& e) {
                std::cerr << "error: " << e.what() << "\n";
                return {1, e.what()};
            }
        }

    } // namespace cli
} // namespace mygit
