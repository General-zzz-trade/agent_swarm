#ifndef PLATFORM_LINUX_LINUX_AGENT_FACTORY_H
#define PLATFORM_LINUX_LINUX_AGENT_FACTORY_H

#include <filesystem>
#include <iosfwd>
#include <memory>

#include "../../app/agent_services.h"

struct AppConfig;
struct AgentCliOptions;
class Agent;

AgentServices create_linux_agent_services(const AppConfig& config,
                                          const AgentCliOptions& options,
                                          std::istream& input,
                                          std::ostream& output);

std::unique_ptr<Agent> create_linux_agent(const std::filesystem::path& workspace_root,
                                          const AppConfig& config,
                                          const AgentCliOptions& options,
                                          std::istream& input,
                                          std::ostream& output);

#endif
