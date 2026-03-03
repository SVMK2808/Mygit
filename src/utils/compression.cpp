#include "mygit/utils/compression.h"

#include <zlib.h>
#include <stdexcept>

namespace mygit {
    namespace utils {

        std::vector<std::byte> Compression::compress(const std::vector<std::byte>& data) {
            uLongf bound = compressBound(static_cast<uLong>(data.size()));
            std::vector<std::byte> out(bound);

            uLongf out_size = bound;
            int rc = ::compress(
                reinterpret_cast<Bytef*>(out.data()),
                &out_size,
                reinterpret_cast<const Bytef*>(data.data()),
                static_cast<uLong>(data.size())
            );

            if (rc != Z_OK) {
                throw std::runtime_error("zlib compress failed: " + std::to_string(rc));
            }

            out.resize(out_size);
            return out;
        }

        std::vector<std::byte> Compression::decompress(const std::vector<std::byte>& data) {
            // Start with 4× the compressed size; grow if needed
            std::vector<std::byte> out(data.size() * 4);

            while (true) {
                uLongf out_size = static_cast<uLongf>(out.size());
                int rc = uncompress(
                    reinterpret_cast<Bytef*>(out.data()),
                    &out_size,
                    reinterpret_cast<const Bytef*>(data.data()),
                    static_cast<uLong>(data.size())
                );

                if (rc == Z_OK) {
                    out.resize(out_size);
                    return out;
                } else if (rc == Z_BUF_ERROR) {
                    // Output buffer too small, double and retry
                    out.resize(out.size() * 2);
                } else {
                    throw std::runtime_error("zlib decompress failed: " + std::to_string(rc));
                }
            }
        }

    } // namespace utils
} // namespace mygit
