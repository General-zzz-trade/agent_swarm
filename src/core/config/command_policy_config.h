#ifndef CORE_CONFIG_COMMAND_POLICY_CONFIG_H
#define CORE_CONFIG_COMMAND_POLICY_CONFIG_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct CommandPolicyConfig {
    std::unordered_set<std::string> allowed_executables = {
        "git",
        "g++", "gcc", "clang", "clang++",
        "cmake", "ctest", "make", "mingw32-make", "ninja",
        "python", "python3", "pip", "pip3",
        "node", "npm", "npx", "yarn", "pnpm",
        "cargo", "rustc",
        "go",
        "javac", "java", "mvn", "gradle",
        "dotnet",
        "ollama",
        "cat", "head", "tail", "wc", "sort", "uniq", "diff",
        "find", "grep", "rg", "fd",
        "ls", "dir", "pwd", "echo", "which", "where",
        "curl", "wget",
        "tar", "zip", "unzip",
    };

    std::unordered_map<std::string, std::unordered_set<std::string>> allowed_subcommands = {
        // Git: full workflow support (except force-push and destructive resets)
        {"git", {"status", "diff", "log", "branch", "rev-parse", "show",
                 "add", "commit", "push", "pull", "fetch", "merge", "rebase",
                 "checkout", "switch", "stash", "reset", "cherry-pick",
                 "tag", "remote", "clone", "init", "config", "blame",
                 "bisect", "clean", "mv", "rm", "apply", "am"}},
        {"ollama", {"list", "ps", "show", "run", "pull"}},
    };

    std::size_t timeout_ms = 60000;        // was 15s — 60s for builds/tests
    std::size_t max_output_bytes = 32000;   // was 4KB — 32KB for test output
};

#endif
