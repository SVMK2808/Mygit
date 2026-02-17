#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace mygit {
    namespace utils {
        class Hash {
            public:
                // Compute SHA-1 hash of data
                static std::string sha1(const std::vector<std::byte>& data);
                static std::string sha1(const std::string& data);

                // Compute hash of file
                static std::string sha1File(const std::string& filePath); 

                // Compute object hash (with Git-style header)
                static std::string objectHash(const std::string& type, 
                                              const std::vector<std::byte>& content);
                
                // Convert hash to hex string 
                static std::string toHex(const unsigned char* data, size_t len);
        };
    }
}