#pragma once

#include "mygit/core/object.h"
#include "mygit/core/types.h"
#include <vector>
#include <string>

namespace mygit {
    class Commit : public Object {
        public:
            Commit(const std::string& tree_hash,
                   const std::vector<std::string>& parent_hashes,
                   const CommitMetadata& metadata);

            ObjectType type() const noexcept override {
                return ObjectType::Commit;
            }

            std::string hash() const override;
            std::vector<std::byte> serialize() const override;
            size_t size() const override;

            // Access methods
            const std::string& treeHash() const { return tree_hash_; }
            const std::vector<std::string>& parentHashes() const { return parent_hashes_; }
            const CommitMetadata& metadata() const { return metadata_; }

            //Deserialize
            static Commit deserialize(const std::vector<std::byte>& data);

        private:
            std::string tree_hash_;
            std::vector<std::string> parent_hashes_;
            CommitMetadata metadata_;
            mutable std::string cached_hash_;
    };
}