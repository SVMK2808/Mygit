#include "mygit/cli/command.h"
#include "mygit/repository/repository.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace mygit {
    namespace cli {

        CommandResult runInit(const Args& args) {
            fs::path target = args.empty() ? fs::current_path() : fs::path(args[0]);

            try {
                fs::create_directories(target);
                Repository::init(target);
                std::cout << "Initialized empty mygit repository in "
                          << (target / ".git").string() << "\n";
                return {0};
            } catch (const std::exception& e) {
                std::cerr << "error: " << e.what() << "\n";
                return {1, e.what()};
            }
        }

    } // namespace cli
} // namespace mygit
