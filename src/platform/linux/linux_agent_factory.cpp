#include "linux_agent_factory.h"

#include <stdexcept>

AgentServices create_linux_agent_services(const AppConfig&,
                                          const AgentCliOptions&,
                                          std::istream&,
                                          std::ostream&) {
    throw std::runtime_error("Linux platform agent services are not implemented yet");
}

std::unique_ptr<Agent> create_linux_agent(const std::filesystem::path&,
                                          const AppConfig&,
                                          const AgentCliOptions&,
                                          std::istream&,
                                          std::ostream&) {
    throw std::runtime_error("Linux platform agent factory is not implemented yet");
}
