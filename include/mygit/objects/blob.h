#pragma once

#include "mygit/core/object.h"
#include <string>
#include <vector>
#include <cstddef>

namespace mygit {

    //Blob object - stores files contents
    class Blob : public Object {
        public:
            explicit Blob(std::vector<std::byte> content);
            explicit Blob(const std::string& content);

            //Load from file
            static Blob fromFile(const std::string& filePath);

            //Object interface
            ObjectType type() const noexcept override { return ObjectType::Blob; }
            std::string hash() const override;
            std::vector<std::byte> serialize() const override;
            size_t size() const override { return content_.size(); }

            //Access content
            const std::vector<std::byte>& content() const { return content_; }
            std::string contentAsString() const;

            //Deserialize
            static Blob deserialize(const std::vector<std::byte>& data);

        private:
            std::vector<std::byte> content_;
            mutable std::string cached_hash_;
    };
} //namespace mygit