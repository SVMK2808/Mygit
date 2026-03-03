#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace mygit {
    namespace cli {

        struct CommandResult {
            int exit_code = 0;
            std::string message{};
        };

        using Args = std::vector<std::string>;

        // Each command receives the remaining argv tokens after the command name
        CommandResult runInit(const Args& args);
        CommandResult runAdd(const Args& args);
        CommandResult runCommit(const Args& args);
        CommandResult runStatus(const Args& args);
        CommandResult runLog(const Args& args);
        CommandResult runBranch(const Args& args);
        CommandResult runCheckout(const Args& args);
        CommandResult runDiff(const Args& args);

    } // namespace cli
} // namespace mygit
