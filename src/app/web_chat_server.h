#ifndef APP_WEB_CHAT_SERVER_H
#define APP_WEB_CHAT_SERVER_H

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <ostream>
#include <vector>

#include "../agent/execution_step.h"
#include "agent_status.h"

class Agent;
class WebApprovalProvider;

class WebChatServer {
public:
    WebChatServer(std::filesystem::path workspace_root,
                  Agent& agent,
                  std::shared_ptr<WebApprovalProvider> approval_provider,
                  unsigned short port);

    int run(std::ostream& output);

private:
    std::filesystem::path workspace_root_;
    Agent& agent_;
    std::shared_ptr<WebApprovalProvider> approval_provider_;
    unsigned short port_;
    std::mutex agent_mutex_;
    std::mutex state_mutex_;
    std::atomic<bool> agent_busy_{false};
    std::vector<ExecutionStep> last_trace_snapshot_;
    std::vector<CapabilityState> capability_snapshot_;
    std::string last_self_check_at_;
};

#endif
