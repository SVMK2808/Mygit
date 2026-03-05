// gc_command.cpp — implementation of `mygit gc`
//
// Runs repository housekeeping:
//   • Packs all loose objects into a single pack file
//     (objects/pack.mgpk + objects/pack.mgpk.idx)
//   • Reports how many objects were packed

#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"

#include <iostream>
#include <stdexcept>

namespace mygit {
    namespace cli {

        CommandResult runGc(const Args& /*args*/) {
            auto root = Repository::findRoot(fs::current_path());
            if (!root) {
                std::cerr << "fatal: not a mygit repository\n";
                return {1};
            }
            auto repo_opt = Repository::open(*root);
            if (!repo_opt) {
                std::cerr << "fatal: could not open repository\n";
                return {1};
            }
            auto& repo = *repo_opt;

            std::cout << "Running garbage collection...\n";
            try {
                const std::size_t packed = repo.objectStore().packObjects();
                if (packed == 0) {
                    std::cout << "Nothing to pack.\n";
                } else {
                    std::cout << "Packed " << packed << " object"
                              << (packed == 1 ? "" : "s") << ".\n";
                    const std::size_t total = repo.objectStore().packCount();
                    std::cout << "Pack now contains " << total << " object"
                              << (total == 1 ? "" : "s") << " total.\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "error during gc: " << e.what() << "\n";
                return {1};
            }

            return {0};
        }

    } // namespace cli
} // namespace mygit
