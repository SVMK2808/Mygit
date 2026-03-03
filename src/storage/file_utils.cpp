#include "mygit/storage/file_utils.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mygit {
    namespace storage {

        std::vector<std::byte> FileUtils::readFile(const fs::path& path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                throw std::runtime_error("Cannot open file for reading: " + path.string());
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            const std::string raw = ss.str();
            std::vector<std::byte> result;
            result.reserve(raw.size());
            for (unsigned char c : raw) {
                result.push_back(static_cast<std::byte>(c));
            }
            return result;
        }

        void FileUtils::writeFile(const fs::path& path, const std::vector<std::byte>& data) {
            ensureDirectory(path.parent_path());
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file) {
                throw std::runtime_error("Cannot open file for writing: " + path.string());
            }
            file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!file) {
                throw std::runtime_error("Write failed for: " + path.string());
            }
        }

        void FileUtils::writeText(const fs::path& path, const std::string& text) {
            ensureDirectory(path.parent_path());
            std::ofstream file(path, std::ios::trunc);
            if (!file) {
                throw std::runtime_error("Cannot open file for writing: " + path.string());
            }
            file << text;
        }

        std::string FileUtils::readText(const fs::path& path) {
            std::ifstream file(path);
            if (!file) {
                throw std::runtime_error("Cannot open file for reading: " + path.string());
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }

        void FileUtils::ensureDirectory(const fs::path& path) {
            if (!path.empty() && !fs::exists(path)) {
                fs::create_directories(path);
            }
        }

        bool FileUtils::isUnder(const fs::path& path, const fs::path& base) {
            auto rel = fs::relative(path, base);
            return !rel.empty() && rel.native()[0] != '.';
        }

        void FileUtils::atomicWrite(const fs::path& path, const std::vector<std::byte>& data) {
            ensureDirectory(path.parent_path());
            const fs::path tmp = fs::path(path.string() + ".tmp");
            {
                std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
                if (!file) throw std::runtime_error("Cannot open tmp file: " + tmp.string());
                file.write(reinterpret_cast<const char*>(data.data()),
                           static_cast<std::streamsize>(data.size()));
                if (!file) throw std::runtime_error("Write failed for: " + tmp.string());
            }
            fs::rename(tmp, path); // atomic on POSIX (same filesystem)
        }

        void FileUtils::atomicWriteText(const fs::path& path, const std::string& text) {
            ensureDirectory(path.parent_path());
            const fs::path tmp = fs::path(path.string() + ".tmp");
            {
                std::ofstream file(tmp, std::ios::trunc);
                if (!file) throw std::runtime_error("Cannot open tmp file: " + tmp.string());
                file << text;
                if (!file) throw std::runtime_error("Write failed for: " + tmp.string());
            }
            fs::rename(tmp, path);
        }

    } // namespace storage
} // namespace mygit
