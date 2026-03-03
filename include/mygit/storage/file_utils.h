#pragma once

#include <filesystem>
#include <vector>
#include <cstddef>
#include <string>

namespace fs = std::filesystem;

namespace mygit {
    namespace storage {

        class FileUtils {
        public:
            // Read all bytes from a file
            static std::vector<std::byte> readFile(const fs::path& path);

            // Write bytes to a file, creating parent directories as needed
            static void writeFile(const fs::path& path, const std::vector<std::byte>& data);

            // Write a string to a file, creating parent directories as needed
            static void writeText(const fs::path& path, const std::string& text);

            // Read a file as a string
            static std::string readText(const fs::path& path);

            // Atomically write bytes to a file (write to .tmp then rename)
        static void atomicWrite(const fs::path& path, const std::vector<std::byte>& data);

        // Atomically write a string to a file
        static void atomicWriteText(const fs::path& path, const std::string& text);

        // Ensure a directory exists (creates recursively if needed)
            static void ensureDirectory(const fs::path& path);

            // Check if a path is inside a directory tree
            static bool isUnder(const fs::path& path, const fs::path& base);
        };

    } // namespace storage
} // namespace mygit
