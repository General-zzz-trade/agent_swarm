#ifndef CORE_INTERFACES_COMMAND_RUNNER_H
#define CORE_INTERFACES_COMMAND_RUNNER_H

#include <cstddef>
#include <filesystem>
#include <string>

struct CommandExecutionResult {
    bool success;
    bool timed_out = false;
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

class ICommandRunner {
public:
    virtual ~ICommandRunner() = default;

    virtual CommandExecutionResult run(const std::string& command,
                                       const std::filesystem::path& working_directory,
                                       std::size_t timeout_ms) const = 0;
};

#endif
