#pragma once

#include <string>
#include <vector>

namespace mygit {
    namespace diff {

        struct Hunk {
            int old_start = 0;   // 1-based
            int old_count = 0;
            int new_start = 0;
            int new_count = 0;
            std::vector<std::string> lines; // each prefixed with ' ', '-', or '+'
        };

        struct FileDiff {
            std::string old_path;
            std::string new_path;
            bool is_new_file  = false;
            bool is_deleted   = false;
            std::vector<Hunk> hunks;
        };

        // Compare two sequences of lines and return diff hunks (3-line context)
        FileDiff computeDiff(const std::string& old_path,
                             const std::string& new_path,
                             const std::vector<std::string>& old_lines,
                             const std::vector<std::string>& new_lines);

        // Print a FileDiff in unified-diff format to stdout
        void printUnifiedDiff(const FileDiff& d);

    } // namespace diff
} // namespace mygit
