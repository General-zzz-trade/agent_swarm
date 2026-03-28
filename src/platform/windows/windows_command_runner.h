#ifndef PLATFORM_WINDOWS_WINDOWS_COMMAND_RUNNER_H
#define PLATFORM_WINDOWS_WINDOWS_COMMAND_RUNNER_H

#include "../../core/interfaces/command_runner.h"

class WindowsCommandRunner : public ICommandRunner {
public:
    CommandExecutionResult run(const std::string& command,
                               const std::filesystem::path& working_directory,
                               std::size_t timeout_ms) const override;
};

#endif
