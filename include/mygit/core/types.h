#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

namespace mygit {

    //Object types
    enum class Objectype : uint8_t {
        Blob,
        Tree, 
        Commit
    };

    // File mode (similar to Git)
    enum class FileMode : uint32_t {
        Regular = 0100644,  //Regular file
        Executable = 0100755, //Executable file
        Directory = 0040000, //Directory
        Symlink = 0120000 //Symbolic link
    };

    //Entry in a tree object
    struct TreeEntry {
        FileMode mode; 
        std::string name; 
        std::string hash; // SHA-1 hash of the object
        Objectype type;
    };

    //Commit metadata
    struct CommitMetadata {
        std::string author_name;
        std::string author_email;
        std::time_t timestamp;
        std::string message;
    };

    //Index entry (staging area)
    struct IndexEntry {
        std::string path;
        std::string hash;
        std::time_t mtime;
        uint64_t file_size;
    };
}