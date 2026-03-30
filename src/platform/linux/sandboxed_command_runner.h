#ifndef PLATFORM_LINUX_SANDBOXED_COMMAND_RUNNER_H
#define PLATFORM_LINUX_SANDBOXED_COMMAND_RUNNER_H

#include <filesystem>
#include <memory>

#include "../../core/interfaces/command_runner.h"
#include "../../core/config/sandbox_config.h"

class SandboxedCommandRunner : public ICommandRunner {
public:
    SandboxedCommandRunner(std::shared_ptr<ICommandRunner> inner,
                           std::filesystem::path workspace_root,
                           SandboxConfig config);

    CommandExecutionResult run(const std::string& command,
                               const std::filesystem::path& working_directory,
                               std::size_t timeout_ms) const override;

    bool is_available() const;  // check if bwrap exists
    const SandboxConfig& config() const { return config_; }

private:
    std::shared_ptr<ICommandRunner> inner_;
    std::filesystem::path workspace_root_;
    SandboxConfig config_;

    std::string build_bwrap_command(const std::string& command,
                                    const std::filesystem::path& working_directory) const;
    static std::string expand_home(const std::string& path);
    static bool command_exists(const std::string& name);
};

#endif
