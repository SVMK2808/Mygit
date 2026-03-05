#pragma once
// merge.h — 3-way merge engine
//
// Provides:
//   • findMergeBase()  — BFS-based LCA on the commit DAG
//   • merge3Way()      — line-level 3-way merge of text content
//   • MergeResult      — the outcome of a 3-way merge

#include "mygit/objects/commit.h"
#include "mygit/storage/object_store.h"

#include <string>
#include <vector>

namespace mygit {
    namespace merge {

        // ─── MergeResult ──────────────────────────────────────────────────
        // Holds the output of a 3-way merge of a single file.
        struct MergeResult {
            std::vector<std::string> lines; // merged content (line-by-line)
            bool has_conflicts = false;     // true if conflict markers were inserted
        };

        // ─── findMergeBase ────────────────────────────────────────────────
        // Return the hash of the nearest common ancestor of commit_a and
        // commit_b in the repository's commit DAG, or an empty string if the
        // two histories are completely unrelated.
        //
        // Algorithm: BFS from commit_a to collect all its ancestors, then BFS
        // from commit_b stopping at the first hash that appears in that set.
        std::string findMergeBase(const ObjectStore& store,
                                  const std::string& commit_a,
                                  const std::string& commit_b);

        // ─── merge3Way ────────────────────────────────────────────────────
        // Perform a line-level 3-way merge.
        //
        //   base_lines   — the common ancestor's content
        //   our_lines    — HEAD (ours) content
        //   their_lines  — incoming branch (theirs) content
        //   our_label    — tag to put in "<<<<<<< <label>" conflict markers
        //   their_label  — tag to put in ">>>>>>> <label>" conflict markers
        //
        // Strategy
        // ────────
        //   1. Compute the Myers edit-scripts base→ours and base→theirs.
        //   2. Convert each script to a list of hunks (base_start, base_len,
        //      replacement_lines[]).
        //   3. Walk through base in order; for overlapping hunks determine
        //      whether the two branches made the same change (clean merge) or
        //      different changes (conflict).
        MergeResult merge3Way(const std::vector<std::string>& base_lines,
                              const std::vector<std::string>& our_lines,
                              const std::vector<std::string>& their_lines,
                              const std::string& our_label   = "HEAD",
                              const std::string& their_label = "theirs");

    } // namespace merge
} // namespace mygit
