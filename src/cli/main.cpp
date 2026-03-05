#include "mygit/cli/command.h"

#include <iostream>
#include <string>
#include <vector>

static void printUsage() {
    std::cout << "MyGit - A lightweight Git implementation in C++\n"
              << "Usage: mygit <command> [args]\n\n"
              << "Commands:\n"
              << "  init [path]              Initialize a new repository\n"
              << "  add <file> [files...]    Stage files for commit\n"
              << "  rm [--cached] <file>...  Remove files from index (and working tree)\n"
              << "  commit -m <message>      Create a commit\n"
              << "  status                   Show working tree status\n"
              << "  log                      Show commit history\n"
              << "  branch [name]            List or create branches\n"
              << "  checkout <branch>        Switch to a branch\n"
              << "  merge <branch>           Merge branch into current branch\n"
              << "  diff [file]              Show changes between working tree and HEAD\n"
              << "  gc                       Pack loose objects (garbage collect)\n"
              << "  help                     Show this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string command = argv[1];
    const std::vector<std::string> args(argv + 2, argv + argc);

    using namespace mygit::cli;

    CommandResult result{1};

    if (command == "init")          result = runInit(args);
    else if (command == "add")      result = runAdd(args);
    else if (command == "rm")       result = runRm(args);
    else if (command == "commit")   result = runCommit(args);
    else if (command == "status")   result = runStatus(args);
    else if (command == "log")      result = runLog(args);
    else if (command == "branch")   result = runBranch(args);
    else if (command == "checkout") result = runCheckout(args);
    else if (command == "merge")    result = runMerge(args);
    else if (command == "diff")     result = runDiff(args);
    else if (command == "gc")       result = runGc(args);
    else if (command == "help")   { printUsage(); return 0; }
    else {
        std::cerr << "mygit: '" << command << "' is not a mygit command. "
                  << "See 'mygit help'.\n";
        return 1;
    }

    return result.exit_code;
}
