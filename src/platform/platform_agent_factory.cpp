#include "platform_agent_factory.h"

#include <stdexcept>
#include <string>

#include "../agent/agent.h"

#ifdef _WIN32
#include "windows/windows_agent_factory.h"
#elif defined(__linux__)
#include "linux/linux_agent_factory.h"
#endif

const char* current_platform_name() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

AgentServices create_platform_agent_services(const AppConfig& config,
                                             const AgentCliOptions& options,
                                             std::istream& input,
                                             std::ostream& output) {
#ifdef _WIN32
    return create_windows_agent_services(config, options, input, output);
#elif defined(__linux__)
    return create_linux_agent_services(config, options, input, output);
#else
    (void)config;
    (void)options;
    (void)input;
    (void)output;
    throw std::runtime_error("No platform agent services are implemented for " +
                             std::string(current_platform_name()));
#endif
}

std::unique_ptr<Agent> create_platform_agent(const std::filesystem::path& workspace_root,
                                             const AppConfig& config,
                                             const AgentCliOptions& options,
                                             std::istream& input,
                                             std::ostream& output) {
#ifdef _WIN32
    return create_windows_agent(workspace_root, config, options, input, output);
#elif defined(__linux__)
    return create_linux_agent(workspace_root, config, options, input, output);
#else
    (void)workspace_root;
    (void)config;
    (void)options;
    (void)input;
    (void)output;
    throw std::runtime_error("No platform agent factory is implemented for " +
                             std::string(current_platform_name()));
#endif
}
