#ifndef APP_AGENT_STATUS_H
#define APP_AGENT_STATUS_H

#include <string>
#include <vector>

struct CapabilityState {
    std::string name;
    std::string label;
    bool implemented = false;
    bool ready = false;
    bool verified = false;
    std::string level;
    std::string detail;
    std::string last_checked_at;
};

struct HealthSnapshot {
    std::string overall;
    std::string model;
    bool busy = false;
    bool approval_pending = false;
    std::size_t implemented_count = 0;
    std::size_t verified_count = 0;
    std::size_t degraded_count = 0;
    std::string last_self_check_at;
};

#endif
