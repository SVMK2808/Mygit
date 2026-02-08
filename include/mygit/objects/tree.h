#pragma once

#include "mygit/core/object.h"
#include "mygit/core/types.h"
#include <map>
#include <string>

namespace mygit {

    //Tree object - stores directory structure
    class Tree : public Object {
        public:
        Tree() = default;

        //Add entry to tree
        void addEntry(const std::string& name, const std::string& hash,
                      FileMode mode, ObjectType type);

        //Object interface
        ObjectType type() const noexcept override { return ObjectType::Tree; }
        std::string hash() const override;
        std::vector<std::byte> serialize() const override;
        size_t size() const override;

        //Access entries
        const std::map<std::string, TreeEntry> & entries() const { return entries_; }
        bool hasEntry(const std::string& name) const;
        const TreeEntry* getEntry(const std::string& name) const;

        //Deserialize
        static Tree deserialize(const std::vector<std::byte>& data);

        private:
            std::map<std::string, TreeEntry> entries_; //sorted by name
            mutable std::string cached_hash_;
    };
}