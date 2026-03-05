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
            if (data.empty()) return {};

            // Start with 4× the compressed size; grow if needed.
            // Cap at 30 doublings (~4 GB) to prevent OOM on corrupt/bomb data.
            constexpr int kMaxDoublings = 30;
            std::vector<std::byte> out(std::max(data.size() * 4, static_cast<size_t>(256)));

            for (int attempt = 0; attempt <= kMaxDoublings; ++attempt) {
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
                    // Output buffer too small — double and retry
                    if (attempt == kMaxDoublings) {
                        throw std::runtime_error(
                            "zlib decompress: output exceeded maximum buffer size (corrupt object?)");
                    }
                    out.resize(out.size() * 2);
                } else {
                    throw std::runtime_error("zlib decompress failed: " + std::to_string(rc));
                }
            }
            // Should never reach here, but satisfy the compiler
            throw std::runtime_error("zlib decompress: unexpected failure");
        }

    } // namespace utils
} // namespace mygit
