#include "sandboxed_command_runner.h"

#include <cstdlib>
#include <sstream>

SandboxedCommandRunner::SandboxedCommandRunner(
    std::shared_ptr<ICommandRunner> inner,
    std::filesystem::path workspace_root,
    SandboxConfig config)
    : inner_(std::move(inner))
    , workspace_root_(std::move(workspace_root))
    , config_(std::move(config)) {
}

CommandExecutionResult SandboxedCommandRunner::run(
    const std::string& command,
    const std::filesystem::path& working_directory,
    std::size_t timeout_ms) const {

    if (!config_.enabled || !is_available()) {
        return inner_->run(command, working_directory, timeout_ms);
    }

    const std::string sandboxed = build_bwrap_command(command, working_directory);
    return inner_->run(sandboxed, {}, timeout_ms);
}

std::string SandboxedCommandRunner::build_bwrap_command(
    const std::string& command,
    const std::filesystem::path& working_directory) const {

    std::ostringstream bwrap;
    bwrap << "bwrap";

    // Base: mount entire filesystem read-only
    bwrap << " --ro-bind / /";

    // Writable overlay: workspace directory
    const std::string ws = workspace_root_.string();
    bwrap << " --bind " << ws << " " << ws;

    // Writable /tmp
    bwrap << " --tmpfs /tmp";

    // Extra writable paths from config
    for (const auto& path : config_.allow_write) {
        const std::string expanded = expand_home(path);
        if (std::filesystem::exists(expanded)) {
            bwrap << " --bind " << expanded << " " << expanded;
        }
    }

    // Block sensitive paths by overlaying empty tmpfs
    // Only overlay paths that actually exist to avoid bwrap mkdir errors
    auto deny_list = config_.deny_read;
    if (deny_list.empty()) {
        deny_list = SandboxConfig::default_deny_read();
    }
    for (const auto& path : deny_list) {
        const std::string expanded = expand_home(path);
        if (std::filesystem::exists(expanded)) {
            bwrap << " --tmpfs " << expanded;
        }
    }

    // Fresh proc and dev
    bwrap << " --proc /proc";
    bwrap << " --dev /dev";

    // PID namespace isolation
    bwrap << " --unshare-pid";
    bwrap << " --die-with-parent";

    // Network isolation (full block when disabled)
    if (!config_.network_enabled) {
        bwrap << " --unshare-net";
    }

    // Working directory
    const std::string wd = working_directory.empty() ? ws : working_directory.string();
    bwrap << " --chdir " << wd;

    // Shell command with proper single-quote escaping
    bwrap << " -- /bin/sh -c '";
    for (char c : command) {
        if (c == '\'') {
            bwrap << "'\\''";
        } else {
            bwrap << c;
        }
    }
    bwrap << "'";

    return bwrap.str();
}

bool SandboxedCommandRunner::is_available() const {
    return command_exists("bwrap");
}

bool SandboxedCommandRunner::command_exists(const std::string& name) {
    return std::filesystem::exists("/usr/bin/" + name) ||
           std::filesystem::exists("/usr/local/bin/" + name);
}

std::string SandboxedCommandRunner::expand_home(const std::string& path) {
    if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + path.substr(1);
    }
    return path;
}
