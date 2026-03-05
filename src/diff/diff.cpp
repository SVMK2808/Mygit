#include "mygit/diff/diff.h"

#include <algorithm>
#include <climits>
#include <iostream>
#include <sstream>

namespace mygit {
    namespace diff {

        // ── Myers O(N+D) shortest-edit-script algorithm ──────────────────
        //
        // Implements the linear-time shortest-edit-script algorithm described
        // by Eugene W. Myers (1986).  Time O((M+N)*D), where D is the edit
        // distance.  Memory O(D²) for the backtrace snapshots.
        //
        // Returns a vector of (op, line) pairs:
        //   op == ' '  → keep (context)
        //   op == '-'  → delete from `a`
        //   op == '+'  → insert from `b`
        //
        // Compact snapshot approach
        // ─────────────────────────
        // During the forward pass we maintain a single working array V:
        //   V[k + OFFSET]  =  furthest x reached on diagonal k
        //                     (where k = x − y).
        // After each round d we save a compact snapshot vs[d] covering
        // only k ∈ [−d, d] (size 2d+1).  These snapshots are used during
        // backtracing; the working V is used only for the forward pass.
        //
        // Memory proof: during backtracing at round d we only access
        // vs[d−1][prev_k + (d−1)] where prev_k = k ± 1 and |k| ≤ d.
        // Interior cases (|k| < d) give |prev_k| ≤ d−1 so the access is
        // always within bounds.

        std::vector<std::pair<char, std::string>>
        computeEditScript(const std::vector<std::string>& a,
                          const std::vector<std::string>& b) {
            const int M = static_cast<int>(a.size());
            const int N = static_cast<int>(b.size());

            // ── trivial cases ─────────────────────────────────────────────
            if (M == 0 && N == 0) return {};
            if (M == 0) {
                std::vector<std::pair<char, std::string>> res;
                res.reserve(N);
                for (const auto& l : b) res.push_back({'+', l});
                return res;
            }
            if (N == 0) {
                std::vector<std::pair<char, std::string>> res;
                res.reserve(M);
                for (const auto& l : a) res.push_back({'-', l});
                return res;
            }

            const int MAX    = M + N;
            const int OFFSET = MAX; // index k+OFFSET maps diagonal k → V array slot

            // Working V array for the forward pass
            std::vector<int> V(2 * MAX + 2, 0);
            V[1 + OFFSET] = 0; // sentinel: on diagonal k=1, furthest x = 0

            // Compact snapshots; vs[d] has 2*d+1 elements, indexed by k+d
            std::vector<std::vector<int>> vs;
            vs.reserve(64);

            int found_d = -1;
            for (int d = 0; d <= MAX; ++d) {
                for (int k = -d; k <= d; k += 2) {
                    int x;
                    if (k == -d ||
                       (k != d && V[k - 1 + OFFSET] < V[k + 1 + OFFSET])) {
                        x = V[k + 1 + OFFSET];      // insert: move from diagonal k+1
                    } else {
                        x = V[k - 1 + OFFSET] + 1;  // delete: move from diagonal k−1
                    }
                    int y = x - k;
                    while (x < M && y < N && a[x] == b[y]) { ++x; ++y; }
                    V[k + OFFSET] = x;
                    if (x == M && y == N) { found_d = d; break; }
                }

                // Save compact snapshot: k ∈ [−d, d]
                std::vector<int> snap(2 * d + 1);
                for (int k = -d; k <= d; ++k) snap[k + d] = V[k + OFFSET];
                vs.push_back(std::move(snap));

                if (found_d >= 0) break;
            }

            if (found_d < 0) {
                // Should never happen (MAX = M+N guarantees a path exists),
                // but defend against it by emitting a trivial edit script.
                std::vector<std::pair<char, std::string>> res;
                for (const auto& l : a) res.push_back({'-', l});
                for (const auto& l : b) res.push_back({'+', l});
                return res;
            }

            // ── backtrace from (M, N) to (0, 0) ──────────────────────────
            std::vector<std::pair<char, std::string>> script;
            script.reserve(M + N);

            int x = M, y = N;
            for (int d = found_d; d > 0; --d) {
                // vs[d−1] covers k ∈ [−(d−1), d−1] with offset pOFF = d−1
                const auto& Vp = vs[d - 1];
                const int pOFF = d - 1;

                const int k = x - y;

                // Determine which move was taken: insert (from k+1) or delete (from k−1)
                bool ins;
                if      (k == -d) ins = true;  // must have come via insert
                else if (k ==  d) ins = false; // must have come via delete
                else ins = (Vp[k - 1 + pOFF] < Vp[k + 1 + pOFF]);

                const int prev_k = ins ? k + 1 : k - 1;
                const int prev_x = Vp[prev_k + pOFF];
                const int prev_y = prev_x - prev_k;

                // Landing position after the single edit
                const int post_x = ins ? prev_x     : prev_x + 1;
                const int post_y = ins ? prev_y + 1 : prev_y;

                // Unwind the snake (matching lines)
                while (x > post_x && y > post_y) {
                    --x; --y;
                    script.push_back({' ', a[x]});
                }

                // Record the edit
                if (ins) { script.push_back({'+', b[--y]}); }
                else      { script.push_back({'-', a[--x]}); }
            }

            // Unwind the initial snake before the first edit
            while (x > 0 && y > 0) { --x; --y; script.push_back({' ', a[x]}); }

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

            const int S = static_cast<int>(script.size());
            std::vector<bool> changed(S, false);
            for (int k = 0; k < S; ++k)
                if (script[k].first != ' ') changed[k] = true;

            // Mark every script line that should appear in any hunk:
            // each changed line pulls in CONTEXT context lines on both sides.
            // Contiguous marked runs become individual hunks — this correctly
            // creates separate hunks for disjoint change groups.
            std::vector<bool> in_hunk(S, false);
            for (int i = 0; i < S; ++i) {
                if (changed[i]) {
                    const int lo = std::max(0, i - CONTEXT);
                    const int hi = std::min(S, i + CONTEXT + 1);
                    for (int j = lo; j < hi; ++j) in_hunk[j] = true;
                }
            }

            // Pre-compute old/new line numbers for every script position
            // so we can quickly look them up when starting a hunk.
            std::vector<int> old_lineno(S + 1, 1), new_lineno(S + 1, 1);
            for (int k = 0; k < S; ++k) {
                old_lineno[k + 1] = old_lineno[k] + (script[k].first != '+' ? 1 : 0);
                new_lineno[k + 1] = new_lineno[k] + (script[k].first != '-' ? 1 : 0);
            }

            int k = 0;
            while (k < S) {
                if (!in_hunk[k]) { ++k; continue; }

                // Start of a contiguous in_hunk run
                const int hunk_start = k;
                while (k < S && in_hunk[k]) ++k;
                const int hunk_end = k;

                Hunk hunk;
                hunk.old_start = old_lineno[hunk_start];
                hunk.new_start = new_lineno[hunk_start];

                for (int m = hunk_start; m < hunk_end; ++m) {
                    const auto [op, line] = script[m];
                    hunk.lines.push_back(std::string(1, op) + line);
                    if (op != '+') ++hunk.old_count;
                    if (op != '-') ++hunk.new_count;
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
