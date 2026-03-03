#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"
#include "mygit/objects/commit.h"

#include <iostream>
#include <iomanip>
#include <ctime>

namespace mygit {
    namespace cli {

        CommandResult runLog(const Args& /*args*/) {
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

            auto current_hash_opt = repo.refs().resolveHead();
            if (!current_hash_opt) {
                std::cout << "No commits yet.\n";
                return {0};
            }

            std::string current_hash = *current_hash_opt;
            constexpr int max_commits = 50; // guard against very long histories
            int count = 0;

            while (!current_hash.empty() && count < max_commits) {
                auto raw = repo.objectStore().load(current_hash);
                if (!raw) {
                    std::cerr << "error: object " << current_hash << " not found\n";
                    return {1};
                }

                const Commit commit = Commit::deserialize(*raw);
                const auto& meta = commit.metadata();

                // Format timestamp
                char time_buf[32];
                std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
                              std::localtime(&meta.timestamp));

                std::cout << "commit " << current_hash << "\n"
                          << "Author: " << meta.author_name
                          << " <" << meta.author_email << ">\n"
                          << "Date:   " << time_buf << "\n"
                          << "\n"
                          << "    " << meta.message << "\n\n";

                if (commit.parentHashes().empty()) break;
                current_hash = commit.parentHashes()[0]; // follow first parent
                ++count;
            }

            return {0};
        }

    } // namespace cli
} // namespace mygit
