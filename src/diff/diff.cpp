#include "mygit/diff/diff.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace mygit {
    namespace diff {

        // ── Myers / LCS-based differ ─────────────────────────────────────
        // Returns an edit script as a vector of pairs: (op, line)
        //   op: ' ' = keep, '-' = delete, '+' = insert
        static std::vector<std::pair<char, std::string>>
        computeEditScript(const std::vector<std::string>& a,
                          const std::vector<std::string>& b) {
            const int M = static_cast<int>(a.size());
            const int N = static_cast<int>(b.size());

            // DP table: lcs[i][j] = LCS length of a[0..i) and b[0..j)
            std::vector<std::vector<int>> dp(M + 1, std::vector<int>(N + 1, 0));
            for (int i = 1; i <= M; ++i) {
                for (int j = 1; j <= N; ++j) {
                    if (a[i-1] == b[j-1]) {
                        dp[i][j] = dp[i-1][j-1] + 1;
                    } else {
                        dp[i][j] = std::max(dp[i-1][j], dp[i][j-1]);
                    }
                }
            }

            // Back-trace to build edit script
            std::vector<std::pair<char, std::string>> script;
            int i = M, j = N;
            while (i > 0 || j > 0) {
                if (i > 0 && j > 0 && a[i-1] == b[j-1]) {
                    script.push_back({' ', a[i-1]});
                    --i; --j;
                } else if (j > 0 && (i == 0 || dp[i][j-1] >= dp[i-1][j])) {
                    script.push_back({'+', b[j-1]});
                    --j;
                } else {
                    script.push_back({'-', a[i-1]});
                    --i;
                }
            }
            std::reverse(script.begin(), script.end());
            return script;
        }

        // ── Hunk builder: groups edit script into hunks with CONTEXT lines ─
        static constexpr int CONTEXT = 3;

        FileDiff computeDiff(const std::string& old_path,
                             const std::string& new_path,
                             const std::vector<std::string>& old_lines,
                             const std::vector<std::string>& new_lines) {
            FileDiff fd;
            fd.old_path = old_path;
            fd.new_path = new_path;
            fd.is_new_file  = old_lines.empty() && !new_lines.empty();
            fd.is_deleted   = !old_lines.empty() && new_lines.empty();

            const auto script = computeEditScript(old_lines, new_lines);
            if (script.empty()) return fd;

            // Identify changed positions to decide which context to include
            // Then group into hunks with CONTEXT lines on each side
            const int S = static_cast<int>(script.size());
            std::vector<bool> changed(S, false);
            for (int k = 0; k < S; ++k) {
                if (script[k].first != ' ') changed[k] = true;
            }

            int k = 0;
            while (k < S) {
                // Skip unchanged lines that are far from any change
                if (!changed[k]) {
                    // Check if any change is within CONTEXT lines ahead
                    bool near = false;
                    for (int c = k; c < std::min(S, k + CONTEXT + 1); ++c) {
                        if (changed[c]) { near = true; break; }
                    }
                    if (!near) { ++k; continue; }
                }

                // Start of a new hunk — go back CONTEXT lines if possible
                int hunk_start = std::max(0, k - CONTEXT);
                // Rewind k to hunk_start
                k = hunk_start;

                Hunk hunk;
                // Determine old/new start lines by counting ops before hunk_start
                int old_line = 1, new_line = 1;
                for (int m = 0; m < hunk_start; ++m) {
                    if (script[m].first != '+') ++old_line;
                    if (script[m].first != '-') ++new_line;
                }
                hunk.old_start = old_line;
                hunk.new_start = new_line;

                int last_change = k;
                for (int m = k; m < S; ++m) {
                    if (changed[m]) last_change = m;
                }
                const int hunk_end = std::min(S, last_change + CONTEXT + 1);

                while (k < hunk_end) {
                    const auto [op, line] = script[k];
                    hunk.lines.push_back(std::string(1, op) + line);
                    if (op != '+') ++hunk.old_count;
                    if (op != '-') ++hunk.new_count;
                    ++k;
                }

                fd.hunks.push_back(std::move(hunk));
            }
            return fd;
        }

        void printUnifiedDiff(const FileDiff& d) {
            if (d.hunks.empty() && !d.is_new_file && !d.is_deleted) {
                return; // no changes
            }

            const std::string old_label = "a/" + d.old_path;
            const std::string new_label = "b/" + d.new_path;

            std::cout << "diff --mygit " << old_label << " " << new_label << "\n";
            if (d.is_new_file) {
                std::cout << "new file mode 100644\n";
                std::cout << "--- /dev/null\n";
                std::cout << "+++ " << new_label << "\n";
            } else if (d.is_deleted) {
                std::cout << "deleted file mode 100644\n";
                std::cout << "--- " << old_label << "\n";
                std::cout << "+++ /dev/null\n";
            } else {
                std::cout << "--- " << old_label << "\n";
                std::cout << "+++ " << new_label << "\n";
            }

            for (const auto& hunk : d.hunks) {
                std::cout << "@@ -" << hunk.old_start << "," << hunk.old_count
                          << " +"  << hunk.new_start << "," << hunk.new_count
                          << " @@\n";
                for (const auto& line : hunk.lines) {
                    std::cout << line << "\n";
                }
            }
        }

    } // namespace diff
} // namespace mygit
