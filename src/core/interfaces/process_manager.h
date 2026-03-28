#ifndef CORE_INTERFACES_PROCESS_MANAGER_H
#define CORE_INTERFACES_PROCESS_MANAGER_H

#include <string>
#include <vector>

struct ProcessInfo {
    unsigned long process_id = 0;
    std::string executable_name;
};

struct ProcessListResult {
    bool success = false;
    std::vector<ProcessInfo> processes;
    std::string error;
};

struct LaunchProcessResult {
    bool success = false;
    unsigned long process_id = 0;
    std::string error;
};

class IProcessManager {
public:
    virtual ~IProcessManager() = default;

    virtual ProcessListResult list_processes() const = 0;
    virtual LaunchProcessResult launch_process(const std::string& command_line) const = 0;
};

#endif
