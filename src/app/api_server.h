#ifndef APP_API_SERVER_H
#define APP_API_SERVER_H

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#include "../agent/execution_step.h"
#include "../core/threading/thread_pool.h"
#include "rate_limiter.h"

class Agent;

class ApiServer {
public:
    ApiServer(std::filesystem::path workspace_root,
              Agent& agent,
              unsigned short port,
              std::string bind_address = "127.0.0.1",
              std::string api_token = {});

    int run(std::ostream& output);

private:
    std::filesystem::path workspace_root_;
    Agent& agent_;
    unsigned short port_;
    std::string bind_address_;
    std::string api_token_;
    std::mutex agent_mutex_;
    std::mutex state_mutex_;
    std::atomic<bool> agent_busy_{false};
    std::vector<ExecutionStep> last_trace_snapshot_;
    ThreadPool thread_pool_;
    RateLimiter rate_limiter_;
};

#endif
