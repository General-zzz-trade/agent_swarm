#ifndef APP_SELF_CHECK_RUNNER_H
#define APP_SELF_CHECK_RUNNER_H

#include <filesystem>
#include <string>
#include <vector>

#include "agent_status.h"

class Agent;

class SelfCheckRunner {
public:
    SelfCheckRunner(Agent& agent, std::filesystem::path workspace_root);

    std::vector<CapabilityState> build_initial_snapshot() const;
    std::vector<CapabilityState> run() const;
    std::string last_checked_at() const;

private:
    Agent& agent_;
    std::filesystem::path workspace_root_;
    mutable std::string last_checked_at_;
};

#endif
