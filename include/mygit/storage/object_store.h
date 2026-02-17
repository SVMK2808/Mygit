#pragma once

#include "mygit/core/object.h"
#include <filesystem>
#include <string>
#include <memory>
#include <optional>

namespace fs = std::filesystem;

namespace mygit {
    //Object storage manager
    class ObjectStore {
        public:
            explicit ObjectStore(const fs::path& objects_dir);

            // Store object
            std::string store(const Object& obj);

            //Load object by hash
            std::optional<std::vector<std::byte>> load(const std::string& hash) const;

            // Check if object exists
            bool exists(const std::string& hash) const;

            //Get object path
            fs::path getObjectPath(const std::string& hash) const;
        
        private:
            fs::path objects_dir_;

            std::pair<std::string, std::string> splitHash(const std::string& hash) const;
    };
}