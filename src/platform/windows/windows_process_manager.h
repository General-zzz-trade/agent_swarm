#ifndef PLATFORM_WINDOWS_WINDOWS_PROCESS_MANAGER_H
#define PLATFORM_WINDOWS_WINDOWS_PROCESS_MANAGER_H

#include "../../core/interfaces/process_manager.h"

class WindowsProcessManager : public IProcessManager {
public:
    ProcessListResult list_processes() const override;
    LaunchProcessResult launch_process(const std::string& command_line) const override;
};

#endif
