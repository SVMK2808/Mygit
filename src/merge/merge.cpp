// merge.cpp — 3-way merge engine implementation

#include "mygit/merge/merge.h"
#include "mygit/diff/diff.h"
#include "mygit/objects/commit.h"

#include <climits>
#include <queue>
#include <stdexcept>
#include <unordered_set>

namespace mygit {
    namespace merge {

        // ─── findMergeBase ────────────────────────────────────────────────
        std::string findMergeBase(const ObjectStore& store,
                                  const std::string& commit_a,
                                  const std::string& commit_b) {
            // BFS phase 1: collect ALL ancestors of commit_a (including itself)
            std::unordered_set<std::string> ancestors_a;
            {
                std::queue<std::string> q;
                q.push(commit_a);
                while (!q.empty()) {
                    const std::string h = q.front(); q.pop();
                    if (h.empty() || ancestors_a.count(h)) continue;
                    ancestors_a.insert(h);
                    auto raw = store.load(h);
                    if (!raw) continue;
                    try {
                        const Commit c = Commit::deserialize(*raw);
                        for (const auto& p : c.parentHashes()) q.push(p);
                    } catch (...) {}
                }
            }

            // BFS phase 2: walk ancestors of commit_b; return first match
            {
                std::queue<std::string> q;
                std::unordered_set<std::string> visited;
                q.push(commit_b);
                while (!q.empty()) {
                    const std::string h = q.front(); q.pop();
                    if (h.empty() || visited.count(h)) continue;
                    visited.insert(h);
                    if (ancestors_a.count(h)) return h; // found!
                    auto raw = store.load(h);
                    if (!raw) continue;
                    try {
                        const Commit c = Commit::deserialize(*raw);
                        for (const auto& p : c.parentHashes()) q.push(p);
                    } catch (...) {}
                }
            }

            return {}; // no common ancestor (unrelated histories)
        }

        // ─── DiffHunk ─────────────────────────────────────────────────────
        // Represents a single changed region in terms of base-file coordinates.
        struct DiffHunk {
            int base_start = 0;            // index of first base line affected (0-based)
            int base_len   = 0;            // how many base lines are replaced
            std::vector<std::string> replacement; // what replaces them
        };

        // ─── editScriptToHunks ────────────────────────────────────────────
        // Convert a Myers edit script into a list of DiffHunks.
        // 'base' positions track index into the original (a) file.
        static std::vector<DiffHunk>
        editScriptToHunks(const std::vector<std::pair<char, std::string>>& script) {
            std::vector<DiffHunk> hunks;
            int base_pos = 0;
            int i = 0;
            const int S = static_cast<int>(script.size());

            while (i < S) {
                if (script[i].first == ' ') { ++base_pos; ++i; continue; }

                // Start of a change region
                const int hunk_base_start = base_pos;
                int hunk_base_len = 0;
                std::vector<std::string> replacement;

                while (i < S && script[i].first != ' ') {
                    if (script[i].first == '-') {
                        ++hunk_base_len;
                        ++base_pos;
                    } else { // '+'
                        replacement.push_back(script[i].second);
                    }
                    ++i;
                }
                hunks.push_back({hunk_base_start, hunk_base_len, std::move(replacement)});
            }
            return hunks;
        }

        // ─── merge3Way ────────────────────────────────────────────────────
        MergeResult merge3Way(const std::vector<std::string>& base_lines,
                              const std::vector<std::string>& our_lines,
                              const std::vector<std::string>& their_lines,
                              const std::string& our_label,
                              const std::string& their_label) {
            // Compute edit scripts base→ours and base→theirs
            const auto our_script   = diff::computeEditScript(base_lines, our_lines);
            const auto their_script = diff::computeEditScript(base_lines, their_lines);

            auto our_hunks   = editScriptToHunks(our_script);
            auto their_hunks = editScriptToHunks(their_script);

            MergeResult result;
            const int N = static_cast<int>(base_lines.size());

            int base_pos = 0;
            size_t oi = 0, ti = 0; // indices into our_hunks / their_hunks

            while (base_pos <= N) {
                const int next_our   = (oi < our_hunks.size())
                                       ? our_hunks[oi].base_start : INT_MAX;
                const int next_their = (ti < their_hunks.size())
                                       ? their_hunks[ti].base_start : INT_MAX;
                const int next_event = std::min({next_our, next_their, N});

                // Flush unchanged base lines up to the next event
                while (base_pos < next_event) {
                    result.lines.push_back(base_lines[base_pos++]);
                }

                if (base_pos >= N && next_our == INT_MAX && next_their == INT_MAX) break;
                if (base_pos > N) break;

                const bool o_active = (oi < our_hunks.size()   &&
                                       our_hunks[oi].base_start   == base_pos);
                const bool t_active = (ti < their_hunks.size() &&
                                       their_hunks[ti].base_start == base_pos);

                if (!o_active && !t_active) {
                    // Flush remaining base lines (shouldn't normally reach here)
                    while (base_pos < N) result.lines.push_back(base_lines[base_pos++]);
                    break;
                }

                if (o_active && t_active) {
                    auto& oh = our_hunks[oi];
                    auto& th = their_hunks[ti];

                    // Determine the combined base range consumed by these two hunks
                    const int combined_end = std::max(oh.base_start + oh.base_len,
                                                      th.base_start + th.base_len);

                    // If they produce identical output → clean auto-merge
                    if (oh.base_len == th.base_len &&
                        oh.replacement == th.replacement) {
                        base_pos += oh.base_len;
                        for (const auto& l : oh.replacement) result.lines.push_back(l);
                        ++oi; ++ti;
                    } else {
                        // Conflict: emit markers
                        result.has_conflicts = true;

                        result.lines.push_back("<<<<<<< " + our_label);
                        for (const auto& l : oh.replacement)
                            result.lines.push_back(l);
                        result.lines.push_back("||||||| base");
                        for (int b = oh.base_start;
                             b < oh.base_start + oh.base_len && b < N; ++b)
                            result.lines.push_back(base_lines[b]);
                        result.lines.push_back("=======");
                        for (const auto& l : th.replacement)
                            result.lines.push_back(l);
                        result.lines.push_back(">>>>>>> " + their_label);

                        base_pos = combined_end;

                        // Advance both hunk iterators past any hunks covered
                        while (oi < our_hunks.size() &&
                               our_hunks[oi].base_start < combined_end) ++oi;
                        while (ti < their_hunks.size() &&
                               their_hunks[ti].base_start < combined_end) ++ti;
                    }

                } else if (o_active) {
                    // Only ours changed: apply cleanly
                    base_pos += our_hunks[oi].base_len;
                    for (const auto& l : our_hunks[oi].replacement)
                        result.lines.push_back(l);
                    ++oi;

                } else { // t_active
                    // Only theirs changed: apply cleanly
                    base_pos += their_hunks[ti].base_len;
                    for (const auto& l : their_hunks[ti].replacement)
                        result.lines.push_back(l);
                    ++ti;
                }
            }

            // Drain any remaining ours-only or theirs-only hunks after base ends
            // (insertions appended at end of file)
            for (; oi < our_hunks.size(); ++oi) {
                for (const auto& l : our_hunks[oi].replacement)
                    result.lines.push_back(l);
            }
            for (; ti < their_hunks.size(); ++ti) {
                // Check overlap with any already-applied ours hunk; simplified:
                // just append theirs insertions that are pure insertions (base_len==0)
                if (their_hunks[ti].base_len == 0) {
                    for (const auto& l : their_hunks[ti].replacement)
                        result.lines.push_back(l);
                }
            }

            return result;
        }

    } // namespace merge
} // namespace mygit
