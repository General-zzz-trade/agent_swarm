#ifndef APP_AGENT_FACTORY_H
#define APP_AGENT_FACTORY_H

#include <filesystem>
#include <memory>

#include "agent_services.h"

struct AppConfig;
struct AgentCliOptions;
class Agent;

std::unique_ptr<Agent> create_agent(const std::filesystem::path& workspace_root,
                                    const AppConfig& config,
                                    const AgentCliOptions& options,
                                    AgentServices services);

#endif
