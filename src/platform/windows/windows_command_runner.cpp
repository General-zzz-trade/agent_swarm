#include "windows_command_runner.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>

namespace {

std::wstring utf8_to_wstring(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        throw std::runtime_error("Failed to convert UTF-8 text to UTF-16");
    }

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length) != length) {
        throw std::runtime_error("Failed to convert UTF-8 text to UTF-16");
    }
    return result;
}

std::string wstring_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int length =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
                            nullptr, nullptr);
    if (length <= 0) {
        return "Failed to convert UTF-16 text to UTF-8";
    }

    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            result.data(), length, nullptr, nullptr) != length) {
        return "Failed to convert UTF-16 text to UTF-8";
    }
    return result;
}

std::string format_windows_error(DWORD error_code) {
    LPWSTR message_buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&message_buffer), 0, nullptr);

    std::wstring message =
        length > 0 && message_buffer != nullptr ? std::wstring(message_buffer, length) : L"";
    if (message_buffer != nullptr) {
        LocalFree(message_buffer);
    }

    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }

    if (message.empty()) {
        return "Windows error " + std::to_string(error_code);
    }
    return wstring_to_utf8(message) + " (code " + std::to_string(error_code) + ")";
}

class TempFile {
public:
    TempFile() {
        std::array<wchar_t, MAX_PATH + 1> temp_path{};
        const DWORD temp_path_length = GetTempPathW(static_cast<DWORD>(temp_path.size()),
                                                    temp_path.data());
        if (temp_path_length == 0 || temp_path_length >= temp_path.size()) {
            throw std::runtime_error("GetTempPathW failed: " +
                                     format_windows_error(GetLastError()));
        }

        std::array<wchar_t, MAX_PATH + 1> temp_file{};
        if (GetTempFileNameW(temp_path.data(), L"mnc", 0, temp_file.data()) == 0) {
            throw std::runtime_error("GetTempFileNameW failed: " +
                                     format_windows_error(GetLastError()));
        }
        path_ = temp_file.data();
    }

    ~TempFile() {
        if (!path_.empty()) {
            DeleteFileW(path_.c_str());
        }
    }

    HANDLE create_write_handle() const {
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;

        HANDLE handle = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("CreateFileW failed: " +
                                     format_windows_error(GetLastError()));
        }
        return handle;
    }

    std::string read_all() const {
        std::ifstream input(std::filesystem::path(path_), std::ios::binary);
        if (!input) {
            return "";
        }

        return std::string((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    }

private:
    std::wstring path_;
};

class AutoHandle {
public:
    explicit AutoHandle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}

    ~AutoHandle() {
        if (valid()) {
            CloseHandle(handle_);
        }
    }

    AutoHandle(const AutoHandle&) = delete;
    AutoHandle& operator=(const AutoHandle&) = delete;

    AutoHandle(AutoHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }

    AutoHandle& operator=(AutoHandle&& other) noexcept {
        if (this != &other) {
            if (valid()) {
                CloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    HANDLE get() const {
        return handle_;
    }

    bool valid() const {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE release() {
        HANDLE handle = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return handle;
    }

private:
    HANDLE handle_;
};

DWORD clamp_timeout(std::size_t timeout_ms) {
    return static_cast<DWORD>(std::min<std::size_t>(
        timeout_ms, static_cast<std::size_t>(INFINITE - 1)));
}

}  // namespace

CommandExecutionResult WindowsCommandRunner::run(
    const std::string& command,
    const std::filesystem::path& working_directory,
    std::size_t timeout_ms) const {
    try {
        TempFile stdout_file;
        TempFile stderr_file;

        AutoHandle stdout_handle(stdout_file.create_write_handle());
        AutoHandle stderr_handle(stderr_file.create_write_handle());

        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;
        AutoHandle stdin_handle(CreateFileW(L"NUL", GENERIC_READ,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!stdin_handle.valid()) {
            return {false, false, -1, "",
                    "Failed to open NUL for command input: " +
                        format_windows_error(GetLastError())};
        }

        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESTDHANDLES;
        startup_info.hStdInput = stdin_handle.get();
        startup_info.hStdOutput = stdout_handle.get();
        startup_info.hStdError = stderr_handle.get();

        PROCESS_INFORMATION process_info{};
        std::wstring command_line = L"cmd.exe /d /c " + utf8_to_wstring(command);
        std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
        mutable_command_line.push_back(L'\0');

        const std::wstring current_directory = working_directory.wstring();
        const BOOL created = CreateProcessW(nullptr, mutable_command_line.data(), nullptr, nullptr,
                                            TRUE, CREATE_NO_WINDOW, nullptr,
                                            current_directory.c_str(), &startup_info,
                                            &process_info);
        if (created == FALSE) {
            return {false, false, -1, "",
                    "Unable to start command: " + format_windows_error(GetLastError())};
        }

        AutoHandle process_handle(process_info.hProcess);
        AutoHandle thread_handle(process_info.hThread);
        stdout_handle = AutoHandle();
        stderr_handle = AutoHandle();
        stdin_handle = AutoHandle();

        const DWORD wait_result = WaitForSingleObject(process_handle.get(), clamp_timeout(timeout_ms));

        bool timed_out = false;
        int exit_code = -1;
        if (wait_result == WAIT_TIMEOUT) {
            timed_out = true;
            TerminateProcess(process_handle.get(), 124);
            WaitForSingleObject(process_handle.get(), 5000);
            exit_code = 124;
        } else if (wait_result == WAIT_OBJECT_0) {
            DWORD process_exit_code = 0;
            if (!GetExitCodeProcess(process_handle.get(), &process_exit_code)) {
                return {false, false, -1, stdout_file.read_all(),
                        "Failed to query command exit code: " +
                            format_windows_error(GetLastError())};
            }
            exit_code = static_cast<int>(process_exit_code);
        } else {
            return {false, false, -1, stdout_file.read_all(),
                    "WaitForSingleObject failed: " + format_windows_error(GetLastError())};
        }

        const std::string stdout_output = stdout_file.read_all();
        std::string stderr_output = stderr_file.read_all();
        if (timed_out && stderr_output.empty()) {
            stderr_output = "Command timed out after " + std::to_string(timeout_ms) + " ms";
        }

        return {exit_code == 0 && !timed_out, timed_out, exit_code, stdout_output, stderr_output};
    } catch (const std::exception& error) {
        return {false, false, -1, "", error.what()};
    }
}
