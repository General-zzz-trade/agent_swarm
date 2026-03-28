#ifndef AGENT_EXECUTION_STEP_H
#define AGENT_EXECUTION_STEP_H

#include <cstddef>
#include <string>

enum class ExecutionStepStatus {
    planned,
    completed,
    failed,
    blocked,
    denied,
};

inline const char* execution_step_status_name(ExecutionStepStatus status) {
    switch (status) {
        case ExecutionStepStatus::planned:
            return "planned";
        case ExecutionStepStatus::completed:
            return "completed";
        case ExecutionStepStatus::failed:
            return "failed";
        case ExecutionStepStatus::blocked:
            return "blocked";
        case ExecutionStepStatus::denied:
            return "denied";
    }
    return "unknown";
}

struct ExecutionStep {
    std::size_t index = 0;
    std::string tool_name;
    std::string args;
    std::string reason;
    std::string risk;
    ExecutionStepStatus status = ExecutionStepStatus::planned;
    std::string detail;
};

#endif
