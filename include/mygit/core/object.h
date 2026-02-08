#pragma once

#include "mygit/core/types.h"
#include <memory>
#include <vector>
#include <cstddef>

namespace mygit {

    //Base class for all Git objects
    class Object {
        public:
            virtual ~Object() = default;

            //Get object type
            virtual ObjectType type() const noexcept = 0;

            //Compute SHA-1 hash
            virtual std::string hash() const = 0;

            //Serialise to bytes
            virtual std::vector<std::byte> serialize() const = 0;

            //Get object size in bytes
            virtual size_t size() const = 0;

            //Non - copyable, movable
            Object(const Object&) = delete;
            Object& operator=(const Object&) = delete;
            Object(Object&&) = default;
            Object& operator=(Object&&) = default;

        protected:
            Object() = default;
    };
} //namespace mygit