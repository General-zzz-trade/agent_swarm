#ifndef PLATFORM_PLATFORM_AGENT_FACTORY_H
#define PLATFORM_PLATFORM_AGENT_FACTORY_H

#include <filesystem>
#include <iosfwd>
#include <memory>

#include "../app/agent_services.h"

struct AppConfig;
struct AgentCliOptions;
class Agent;

const char* current_platform_name();

AgentServices create_platform_agent_services(const AppConfig& config,
                                             const AgentCliOptions& options,
                                             std::istream& input,
                                             std::ostream& output);

std::unique_ptr<Agent> create_platform_agent(const std::filesystem::path& workspace_root,
                                             const AppConfig& config,
                                             const AgentCliOptions& options,
                                             std::istream& input,
                                             std::ostream& output);

#endif
