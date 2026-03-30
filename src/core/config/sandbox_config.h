#ifndef CORE_CONFIG_SANDBOX_CONFIG_H
#define CORE_CONFIG_SANDBOX_CONFIG_H

#include <string>
#include <vector>

struct SandboxConfig {
    bool enabled = false;
    bool auto_allow_bash = false;  // auto-approve bash when sandboxed

    // Filesystem
    std::vector<std::string> allow_write;   // extra writable paths
    std::vector<std::string> deny_write;    // blocked write paths
    std::vector<std::string> deny_read;     // blocked read paths (e.g. ~/.ssh)

    // Network
    bool network_enabled = true;            // allow network access
    std::vector<std::string> allowed_domains; // domain whitelist (empty = allow all)

    // Defaults for deny_read (sensitive paths)
    static std::vector<std::string> default_deny_read() {
        return {"~/.ssh", "~/.aws", "~/.gnupg", "~/.config/gh",
                "/etc/shadow", "/etc/gshadow"};
    }
};

#endif
