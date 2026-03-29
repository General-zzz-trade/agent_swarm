#include "linux_process_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

ProcessListResult LinuxProcessManager::list_processes() const {
    ProcessListResult result;
    result.success = true;

#ifdef __APPLE__
    // macOS: use 'ps' command as fallback
    FILE* pipe = popen("ps -eo pid,comm", "r");
    if (!pipe) {
        result.success = false;
        result.error = "Cannot run ps";
        return result;
    }
    char line[256];
    fgets(line, sizeof(line), pipe);  // Skip header
    while (fgets(line, sizeof(line), pipe)) {
        unsigned long pid = 0;
        char name[256] = {};
        if (sscanf(line, "%lu %255s", &pid, name) == 2) {
            result.processes.push_back({pid, name});
        }
    }
    pclose(pipe);
#else
    // Linux: read /proc directly
    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) {
        result.success = false;
        result.error = "Cannot open /proc";
        return result;
    }

    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        bool is_pid = true;
        for (const char* c = entry->d_name; *c; ++c) {
            if (*c < '0' || *c > '9') { is_pid = false; break; }
        }
        if (!is_pid) continue;

        const unsigned long pid = std::strtoul(entry->d_name, nullptr, 10);
        std::ifstream comm_file("/proc/" + std::string(entry->d_name) + "/comm");
        std::string name;
        if (comm_file && std::getline(comm_file, name)) {
            result.processes.push_back({pid, name});
        }
    }
    closedir(proc_dir);
#endif

    return result;
}

LaunchProcessResult LinuxProcessManager::launch_process(
    const std::string& command_line) const {

    const pid_t pid = fork();
    if (pid < 0) {
        return {false, 0, "fork() failed: " + std::string(std::strerror(errno))};
    }

    if (pid == 0) {
        // Child: detach and exec
        setsid();
        execl("/bin/sh", "sh", "-c", command_line.c_str(), nullptr);
        _exit(127);
    }

    return {true, static_cast<unsigned long>(pid), ""};
}
