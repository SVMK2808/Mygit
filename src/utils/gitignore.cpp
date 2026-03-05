#include "mygit/utils/gitignore.h"
#include "mygit/storage/file_utils.h"

#include <sstream>

namespace mygit {
    namespace utils {

        GitIgnore::GitIgnore(const fs::path& repo_root) {
            const fs::path gi_path = repo_root / ".gitignore";
            if (!fs::exists(gi_path)) return;

            try {
                const std::string text = storage::FileUtils::readText(gi_path);
                std::istringstream iss(text);
                std::string line;
                while (std::getline(iss, line)) {
                    // Strip trailing carriage return
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    // Skip blanks and comments
                    if (line.empty() || line[0] == '#') continue;
                    patterns_.push_back(line);
                }
            } catch (...) {}
        }

        bool GitIgnore::isIgnored(const std::string& rel_path) const {
            for (const auto& pattern : patterns_) {
                if (matchPattern(pattern, rel_path)) return true;
            }
            return false;
        }

        // Very small glob matcher supporting:
        //   *     — any sequence of non-separator chars
        //   **    — any sequence of chars (including /)
        //   ?     — any single non-separator char
        //   trailing / — directory pattern (we ignore that distinction here)
        bool GitIgnore::matchPattern(const std::string& pattern,
                                     const std::string& rel_path) {
            // Normalise: strip leading '/'
            const std::string& pat = (pattern[0] == '/') ? pattern.substr(1) : pattern;

            // Directory pattern: trailing '/' means "anything inside this directory"
            if (!pat.empty() && pat.back() == '/') {
                const std::string dir_name = pat.substr(0, pat.size() - 1);

                // If the pattern has no '/', match the directory name against ANY path component.
                // e.g. pattern "build/" matches "build/foo", "src/build/foo", "a/b/build/c"
                if (dir_name.find('/') == std::string::npos) {
                    // Check each path component
                    // rel_path looks like "src/build/foo.cpp"
                    std::string remaining = rel_path;
                    while (!remaining.empty()) {
                        const size_t slash = remaining.find('/');
                        const std::string component = (slash == std::string::npos)
                                                      ? remaining
                                                      : remaining.substr(0, slash);
                        if (component == dir_name) return true;
                        if (slash == std::string::npos) break;
                        remaining = remaining.substr(slash + 1);
                    }
                    return false;
                }

                // Anchored directory pattern: match from root
                const std::string dir_prefix = dir_name + "/";
                if (rel_path.substr(0, dir_prefix.size()) == dir_prefix) return true;
                if (rel_path == dir_name) return true;
                return false;
            }

            // If pattern has no '/', match against the basename only
            const bool anchored = pat.find('/') != std::string::npos;
            const std::string& subject = anchored ? rel_path
                                                   : fs::path(rel_path).filename().string();

            // Simple recursive glob match
            std::function<bool(size_t, size_t)> match = [&](size_t pi, size_t si) -> bool {
                while (pi < pat.size() && si <= subject.size()) {
                    if (pi + 1 < pat.size() && pat[pi] == '*' && pat[pi+1] == '*') {
                        // ** — try matching any suffix
                        pi += 2;
                        if (pi < pat.size() && pat[pi] == '/') ++pi;
                        for (size_t i = si; i <= subject.size(); ++i) {
                            if (match(pi, i)) return true;
                        }
                        return false;
                    } else if (pat[pi] == '*') {
                        ++pi;
                        for (size_t i = si; i <= subject.size(); ++i) {
                            if (subject[i] == '/' && i != si) break;
                            if (match(pi, i)) return true;
                        }
                        return false;
                    } else if (pat[pi] == '?') {
                        if (si >= subject.size() || subject[si] == '/') return false;
                        ++pi; ++si;
                    } else {
                        if (si >= subject.size() || pat[pi] != subject[si]) return false;
                        ++pi; ++si;
                    }
                }
                // Skip trailing '/' in pattern
                while (pi < pat.size() && pat[pi] == '/') ++pi;
                return pi == pat.size() && si == subject.size();
            };

            return match(0, 0);
        }

    } // namespace utils
} // namespace mygit
