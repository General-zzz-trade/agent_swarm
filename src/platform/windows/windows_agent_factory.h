#ifndef PLATFORM_WINDOWS_WINDOWS_AGENT_FACTORY_H
#define PLATFORM_WINDOWS_WINDOWS_AGENT_FACTORY_H

#include <filesystem>
#include <iosfwd>
#include <memory>

#include "../../app/agent_services.h"

struct AppConfig;
struct AgentCliOptions;
class Agent;

AgentServices create_windows_agent_services(const AppConfig& config,
                                            const AgentCliOptions& options,
                                            std::istream& input,
                                            std::ostream& output);

std::unique_ptr<Agent> create_windows_agent(const std::filesystem::path& workspace_root,
                                            const AppConfig& config,
                                            const AgentCliOptions& options,
                                            std::istream& input,
                                            std::ostream& output);

#endif
