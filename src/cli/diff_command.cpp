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
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
            return lines;
        }

        // Returns true if the first `scan_bytes` bytes of `data` contain a null byte,
        // which is a reliable signal that the content is binary.
        static bool isBinary(const std::vector<std::byte>& data,
                              size_t scan_bytes = 8192) {
            const size_t limit = std::min(data.size(), scan_bytes);
            for (size_t i = 0; i < limit; ++i) {
                if (data[i] == static_cast<std::byte>('\0')) return true;
            }
            return false;
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

            // Collect the (path -> blob_hash) mapping from the last commit's tree.
            // Uses getCommittedFiles() which recurses into subdirectories.
            const auto committed_hashes = repo.getCommittedFiles();

            const auto& entries = repo.index().entries();

            // Determine which files to diff.
            // If args are given, only diff those paths; otherwise diff all staged + committed.
            std::vector<std::string> targets;
            if (!args.empty()) {
                for (const auto& a : args) {
                    std::error_code ec;
                    const auto rel = fs::relative(fs::absolute(a), repo.workDir(), ec);
                    if (!ec) {
                        targets.push_back(rel.generic_string());
                    } else {
                        targets.push_back(a);
                    }
                }
            } else {
                for (const auto& e : entries) targets.push_back(e.path);
                // Also include committed files that are no longer in the index (deletions)
                for (const auto& [committed_path, _] : committed_hashes) {
                    bool already = false;
                    for (const auto& t : targets) {
                        if (t == committed_path) { already = true; break; }
                    }
                    if (!already) targets.push_back(committed_path);
                }
            }

            bool any_diff = false;
            for (const auto& rel_path : targets) {
                const fs::path abs_path = repo.workDir() / rel_path;

                // Old content: from last commit
                std::vector<std::byte> old_bytes;
                auto it = committed_hashes.find(rel_path);
                if (it != committed_hashes.end()) {
                    auto blob_raw = repo.objectStore().load(it->second);
                    if (blob_raw) old_bytes = std::move(*blob_raw);
                }

                // New content: from working tree
                std::vector<std::byte> new_bytes;
                if (fs::exists(abs_path)) {
                    try {
                        const Blob blob = Blob::fromFile(abs_path.string());
                        new_bytes = blob.serialize();
                    } catch (...) {}
                }

                if (old_bytes == new_bytes) continue;
                any_diff = true;

                // Binary file detection
                if (isBinary(old_bytes) || isBinary(new_bytes)) {
                    std::cout << "Binary files a/" << rel_path
                              << " and b/" << rel_path << " differ\n";
                    continue;
                }

                // Convert to string for line-based diff
                auto bytesToStr = [](const std::vector<std::byte>& b) {
                    std::string s;
                    s.reserve(b.size());
                    for (std::byte by : b) s.push_back(static_cast<char>(by));
                    return s;
                };

                const auto old_lines = toLines(bytesToStr(old_bytes));
                const auto new_lines = toLines(bytesToStr(new_bytes));

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
