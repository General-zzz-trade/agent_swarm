#include "linux_command_runner.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct PipeHandle {
    int fd[2] = {-1, -1};
    bool open() { return pipe(fd) == 0; }
    void close_read() { if (fd[0] >= 0) { ::close(fd[0]); fd[0] = -1; } }
    void close_write() { if (fd[1] >= 0) { ::close(fd[1]); fd[1] = -1; } }
    ~PipeHandle() { close_read(); close_write(); }
};

std::string read_fd(int fd) {
    std::string result;
    std::array<char, 4096> buf;
    while (true) {
        const ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n <= 0) break;
        result.append(buf.data(), static_cast<std::size_t>(n));
        if (result.size() > 1024 * 1024) break;  // 1MB safety limit
    }
    return result;
}

}  // namespace

CommandExecutionResult LinuxCommandRunner::run(
    const std::string& command,
    const std::filesystem::path& working_directory,
    std::size_t timeout_ms) const {

    CommandExecutionResult result;
    result.success = false;
    result.timed_out = false;
    result.exit_code = -1;

    PipeHandle stdout_pipe, stderr_pipe;
    if (!stdout_pipe.open() || !stderr_pipe.open()) {
        result.stderr_output = "Failed to create pipes: " + std::string(std::strerror(errno));
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        result.stderr_output = "fork() failed: " + std::string(std::strerror(errno));
        return result;
    }

    if (pid == 0) {
        // Child process
        stdout_pipe.close_read();
        stderr_pipe.close_read();
        dup2(stdout_pipe.fd[1], STDOUT_FILENO);
        dup2(stderr_pipe.fd[1], STDERR_FILENO);
        stdout_pipe.close_write();
        stderr_pipe.close_write();

        if (!working_directory.empty()) {
            if (chdir(working_directory.c_str()) != 0) {
                _exit(127);
            }
        }

        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    // Parent process
    stdout_pipe.close_write();
    stderr_pipe.close_write();

    // Set non-blocking for timeout support
    fcntl(stdout_pipe.fd[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe.fd[0], F_SETFL, O_NONBLOCK);

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);

    // Wait for process with timeout
    bool done = false;
    int status = 0;
    while (!done && std::chrono::steady_clock::now() < deadline) {
        const pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            done = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    if (!done) {
        // Timeout: kill the process
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        result.timed_out = true;
    }

    // Read output (set blocking again)
    fcntl(stdout_pipe.fd[0], F_SETFL, 0);
    fcntl(stderr_pipe.fd[0], F_SETFL, 0);
    result.stdout_output = read_fd(stdout_pipe.fd[0]);
    result.stderr_output = read_fd(stderr_pipe.fd[0]);

    if (done && WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        result.success = (result.exit_code == 0);
    }

    return result;
}
