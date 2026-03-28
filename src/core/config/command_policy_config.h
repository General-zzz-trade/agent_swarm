#ifndef CORE_CONFIG_COMMAND_POLICY_CONFIG_H
#define CORE_CONFIG_COMMAND_POLICY_CONFIG_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct CommandPolicyConfig {
    std::unordered_set<std::string> allowed_executables = {
        "git",
        "g++",
        "cmake",
        "ctest",
        "ollama",
    };

    std::unordered_map<std::string, std::unordered_set<std::string>> allowed_subcommands = {
        {"git", {"status", "diff", "log", "branch", "rev-parse", "show"}},
        {"ollama", {"list", "ps", "show"}},
    };

    std::size_t timeout_ms = 15000;
    std::size_t max_output_bytes = 4000;
};

#endif
