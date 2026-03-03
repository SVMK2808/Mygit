#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace mygit {
    namespace utils {

        class GitIgnore {
        public:
            // Load patterns from a .gitignore file (if it exists)
            explicit GitIgnore(const fs::path& repo_root);

            // Returns true if the relative path should be ignored
            bool isIgnored(const std::string& rel_path) const;

        private:
            std::vector<std::string> patterns_;

            // Match a single pattern against a path component or full path
            static bool matchPattern(const std::string& pattern,
                                     const std::string& rel_path);
        };

    } // namespace utils
} // namespace mygit
