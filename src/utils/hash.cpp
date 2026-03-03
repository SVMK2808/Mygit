#include "mygit/utils/hash.h"

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace mygit {
    namespace utils {

        std::string Hash::sha1(const std::vector<std::byte>& data) {
            unsigned char digest[SHA_DIGEST_LENGTH];
            SHA1(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest);
            return toHex(digest, SHA_DIGEST_LENGTH);
        }

        std::string Hash::sha1(const std::string& data) {
            unsigned char digest[SHA_DIGEST_LENGTH];
            SHA1(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest);
            return toHex(digest, SHA_DIGEST_LENGTH);
        }

        std::string Hash::sha1File(const std::string& filePath) {
            std::ifstream file(filePath, std::ios::binary);
            if (!file) {
                throw std::runtime_error("Cannot open file: " + filePath);
            }

            // Use EVP API (OpenSSL 3 deprecated the low-level SHA1_* functions)
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");

            if (EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) != 1) {
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestInit_ex failed");
            }

            char buf[8192];
            while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
                EVP_DigestUpdate(ctx, buf, static_cast<size_t>(file.gcount()));
            }

            unsigned char digest[EVP_MAX_MD_SIZE];
            unsigned int digest_len = 0;
            EVP_DigestFinal_ex(ctx, digest, &digest_len);
            EVP_MD_CTX_free(ctx);
            return toHex(digest, digest_len);
        }

        std::string Hash::objectHash(const std::string& type, const std::vector<std::byte>& content) {
            // Git format: "<type> <size>\0<content>"
            std::string header = type + " " + std::to_string(content.size()) + '\0';

            std::vector<std::byte> full;
            full.reserve(header.size() + content.size());
            for (char c : header) {
                full.push_back(static_cast<std::byte>(c));
            }
            full.insert(full.end(), content.begin(), content.end());

            return sha1(full);
        }

        std::string Hash::toHex(const unsigned char* data, size_t len) {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (size_t i = 0; i < len; ++i) {
                oss << std::setw(2) << static_cast<unsigned>(data[i]);
            }
            return oss.str();
        }

    } // namespace utils
} // namespace mygit
