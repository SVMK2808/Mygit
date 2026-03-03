#include "mygit/objects/commit.h"
#include "mygit/utils/hash.h"

#include <sstream>
#include <stdexcept>

namespace mygit {

    Commit::Commit(const std::string& tree_hash,
                   const std::vector<std::string>& parent_hashes,
                   const CommitMetadata& metadata)
        : tree_hash_(tree_hash)
        , parent_hashes_(parent_hashes)
        , metadata_(metadata) {}

    std::string Commit::hash() const {
        if (cached_hash_.empty()) {
            cached_hash_ = utils::Hash::objectHash("commit", serialize());
        }
        return cached_hash_;
    }

    // Serialise in a Git-inspired text format:
    //   tree <hash>
    //   parent <hash>       (one line per parent; omitted for root commits)
    //   author <name> <email> <unix_timestamp>
    //   committer <name> <email> <unix_timestamp>
    //                          (blank line)
    //   <message>
    std::vector<std::byte> Commit::serialize() const {
        std::ostringstream oss;

        oss << "tree " << tree_hash_ << "\n";

        for (const auto& p : parent_hashes_) {
            oss << "parent " << p << "\n";
        }

        oss << "author " << metadata_.author_name
            << " <" << metadata_.author_email << "> "
            << static_cast<long long>(metadata_.timestamp) << "\n";

        oss << "committer " << metadata_.author_name
            << " <" << metadata_.author_email << "> "
            << static_cast<long long>(metadata_.timestamp) << "\n";

        oss << "\n";
        oss << metadata_.message << "\n";

        const std::string s = oss.str();
        std::vector<std::byte> out;
        out.reserve(s.size());
        for (char c : s) out.push_back(static_cast<std::byte>(c));
        return out;
    }

    size_t Commit::size() const {
        return serialize().size();
    }

    Commit Commit::deserialize(const std::vector<std::byte>& data) {
        std::string text;
        text.reserve(data.size());
        for (std::byte b : data) text.push_back(static_cast<char>(b));

        std::istringstream iss(text);
        std::string line;

        std::string tree_hash;
        std::vector<std::string> parents;
        CommitMetadata meta{};
        bool in_message = false;
        std::ostringstream message_buf;

        while (std::getline(iss, line)) {
            if (in_message) {
                message_buf << line << "\n";
                continue;
            }
            if (line.empty()) {
                in_message = true;
                continue;
            }

            if (line.rfind("tree ", 0) == 0) {
                tree_hash = line.substr(5);
            } else if (line.rfind("parent ", 0) == 0) {
                parents.push_back(line.substr(7));
            } else if (line.rfind("author ", 0) == 0) {
                // author <name> <email> <timestamp>
                // Find last space for timestamp, second-to-last '>' for email
                std::string rest = line.substr(7);
                auto ts_sep = rest.rfind(' ');
                if (ts_sep != std::string::npos) {
                    meta.timestamp = static_cast<std::time_t>(std::stoll(rest.substr(ts_sep + 1)));
                    rest = rest.substr(0, ts_sep);
                }
                auto email_end = rest.rfind('>');
                auto email_start = rest.rfind('<');
                if (email_start != std::string::npos && email_end != std::string::npos) {
                    meta.author_email = rest.substr(email_start + 1, email_end - email_start - 1);
                    meta.author_name = rest.substr(0, email_start - 1); // trim trailing space
                }
            }
            // committer line is ignored (same as author in this implementation)
        }

        std::string msg = message_buf.str();
        // Strip trailing newline added during serialization
        if (!msg.empty() && msg.back() == '\n') msg.pop_back();
        meta.message = msg;

        return Commit(tree_hash, parents, meta);
    }

} // namespace mygit
