#pragma once

#include <vector>
#include <cstddef>

namespace mygit {
    namespace utils {

        class Compression {
        public:
            // Deflate compress
            static std::vector<std::byte> compress(const std::vector<std::byte>& data);

            // Inflate decompress
            static std::vector<std::byte> decompress(const std::vector<std::byte>& data);
        };

    } // namespace utils
} // namespace mygit
