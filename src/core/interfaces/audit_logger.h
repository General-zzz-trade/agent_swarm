#ifndef CORE_INTERFACES_AUDIT_LOGGER_H
#define CORE_INTERFACES_AUDIT_LOGGER_H

#include <cstddef>
#include <string>

struct AuditEvent {
    std::string category;
    std::string stage;
    std::string tool_name;
    std::string target;
    std::string workspace_root;
    std::size_t timeout_ms = 0;
    int exit_code = -1;
    bool approved = false;
    bool success = false;
    bool timed_out = false;
    std::string detail;
};

class IAuditLogger {
public:
    virtual ~IAuditLogger() = default;
    virtual void log(const AuditEvent& event) = 0;
};

#endif
