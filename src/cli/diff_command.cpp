#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"
#include "mygit/objects/blob.h"
#include "mygit/objects/commit.h"
#include "mygit/objects/tree.h"
#include "mygit/diff/diff.h"

#include <iostream>
#include <sstream>

namespace mygit {
    namespace cli {

        // Split file content (string) into lines, preserving empty lines
        static std::vector<std::string> toLines(const std::string& text) {
            std::vector<std::string> lines;
            std::istringstream iss(text);
            std::string line;
            while (std::getline(iss, line)) {
                // Strip trailing \r for cross-platform consistency
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
            return lines;
        }

        CommandResult runDiff(const Args& args) {
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

            // Resolve the HEAD commit tree (may be absent on unborn repos)
            std::optional<std::string> head_hash = repo.refs().resolveHead();

            // Collect the (path -> blob_hash) mapping from the last commit's tree
            std::unordered_map<std::string, std::string> committed_hashes;
            if (head_hash) {
                auto commit_raw = repo.objectStore().load(*head_hash);
                if (commit_raw) {
                    const auto commit = Commit::deserialize(*commit_raw);
                    auto tree_raw = repo.objectStore().load(commit.treeHash());
                    if (tree_raw) {
                        // Flatten the tree (single level for now; recursive support
                        // matches the current tree builder)
                        const auto tree = Tree::deserialize(*tree_raw);
                        for (const auto& [name, entry] : tree.entries()) {
                            committed_hashes[name] = entry.hash;
                        }
                    }
                }
            }

            const auto& entries = repo.index().entries();

            // Determine which files to diff
            // If args are given, only diff those paths; otherwise diff all staged
            std::vector<std::string> targets;
            if (!args.empty()) {
                for (const auto& a : args) {
                    targets.push_back(fs::relative(fs::absolute(a), repo.workDir()).string());
                }
            } else {
                for (const auto& e : entries) targets.push_back(e.path);
                // Also include files deleted from working tree that were committed
                for (const auto& [path, _] : committed_hashes) {
                    if (std::find(targets.begin(), targets.end(), path) == targets.end()) {
                        targets.push_back(path);
                    }
                }
            }

            bool any_diff = false;
            for (const auto& rel_path : targets) {
                const fs::path abs_path = repo.workDir() / rel_path;

                // Old content: from last commit
                std::vector<std::string> old_lines;
                auto it = committed_hashes.find(rel_path);
                if (it != committed_hashes.end()) {
                    auto blob_raw = repo.objectStore().load(it->second);
                    if (blob_raw) {
                        std::string content;
                        for (std::byte b : *blob_raw) content += static_cast<char>(b);
                        old_lines = toLines(content);
                    }
                }

                // New content: from working tree
                std::vector<std::string> new_lines;
                if (fs::exists(abs_path)) {
                    try {
                        const Blob blob = Blob::fromFile(abs_path.string());
                        new_lines = toLines(blob.contentAsString());
                    } catch (...) {}
                }

                if (old_lines == new_lines) continue;
                any_diff = true;

                const auto fd = diff::computeDiff(rel_path, rel_path, old_lines, new_lines);
                diff::printUnifiedDiff(fd);
            }

            if (!any_diff) {
                std::cout << "(no changes)\n";
            }

            return {0};
        }

    } // namespace cli
} // namespace mygit
