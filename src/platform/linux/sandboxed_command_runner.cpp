#include "sandboxed_command_runner.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace {

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

std::string seatbelt_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

}  // namespace

SandboxedCommandRunner::SandboxedCommandRunner(
    std::shared_ptr<ICommandRunner> inner,
    std::filesystem::path workspace_root,
    SandboxConfig config)
    : inner_(std::move(inner))
    , workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root)))
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

#ifdef __APPLE__
    return build_seatbelt_command(command, working_directory);
#else
    return build_bwrap_linux_command(command, working_directory);
#endif
}

#ifdef __APPLE__

std::string SandboxedCommandRunner::build_seatbelt_command(
    const std::string& command,
    const std::filesystem::path& working_directory) const {

    const std::string ws = workspace_root_.string();
    const std::string wd = working_directory.empty() ? ws : working_directory.string();

    // Build Seatbelt profile
    std::ostringstream profile;
    profile << "(version 1)\n";
    profile << "(deny default)\n";

    // Allow read everywhere (then deny specific paths below)
    profile << "(allow file-read*)\n";

    // Allow write to workspace and /tmp
    profile << "(allow file-write* (subpath \"" << seatbelt_escape(ws) << "\"))\n";
    profile << "(allow file-write* (subpath \"/tmp\"))\n";
    profile << "(allow file-write* (subpath \"/private/tmp\"))\n";

    // Extra writable paths from config
    for (const auto& path : config_.allow_write) {
        const std::string expanded = expand_home(path);
        profile << "(allow file-write* (subpath \"" << seatbelt_escape(expanded) << "\"))\n";
    }

    // Deny sensitive read paths
    auto deny_list = config_.deny_read;
    if (deny_list.empty()) {
        deny_list = SandboxConfig::default_deny_read();
    }
    for (const auto& path : deny_list) {
        const std::string expanded = expand_home(path);
        profile << "(deny file-read* (subpath \"" << seatbelt_escape(expanded) << "\"))\n";
    }

    // Process and system calls required for most tools to work
    profile << "(allow process-exec)\n";
    profile << "(allow process-fork)\n";
    profile << "(allow sysctl-read)\n";
    profile << "(allow mach-lookup)\n";
    profile << "(allow signal)\n";
    profile << "(allow iokit-open)\n";

    // Network
    if (config_.network_enabled) {
        profile << "(allow network*)\n";
    } else {
        profile << "(deny network*)\n";
    }

    // Build the sandbox-exec command
    std::ostringstream cmd;
    const std::string script = "cd " + shell_quote(wd) + " && exec " + command;
    cmd << "sandbox-exec -p " << shell_quote(profile.str())
        << " /bin/sh -lc " << shell_quote(script);

    return cmd.str();
}

#else

std::string SandboxedCommandRunner::build_bwrap_linux_command(
    const std::string& command,
    const std::filesystem::path& working_directory) const {

    std::ostringstream bwrap;
    bwrap << "bwrap";

    // Base: mount entire filesystem read-only
    bwrap << " --ro-bind / /";

    // Writable overlay: workspace directory
    const std::string ws = workspace_root_.string();
    bwrap << " --bind " << shell_quote(ws) << " " << shell_quote(ws);

    // Writable /tmp
    bwrap << " --tmpfs /tmp";

    // Extra writable paths from config
    for (const auto& path : config_.allow_write) {
        const std::string expanded = expand_home(path);
        if (std::filesystem::exists(expanded)) {
            bwrap << " --bind " << shell_quote(expanded) << " " << shell_quote(expanded);
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
            bwrap << " --tmpfs " << shell_quote(expanded);
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
    bwrap << " --chdir " << shell_quote(wd);

    bwrap << " -- /bin/sh -lc " << shell_quote(command);

    return bwrap.str();
}

#endif

bool SandboxedCommandRunner::is_available() const {
#ifdef __APPLE__
    return command_exists("sandbox-exec");
#else
    return command_exists("bwrap");
#endif
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
