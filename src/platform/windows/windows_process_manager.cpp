#include "windows_process_manager.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>

namespace {

std::wstring utf8_to_wstring(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
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

    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr,
                                           nullptr);
    if (length <= 0) {
        throw std::runtime_error("Failed to convert UTF-16 text to UTF-8");
    }

    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            result.data(), length, nullptr, nullptr) != length) {
        throw std::runtime_error("Failed to convert UTF-16 text to UTF-8");
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

    bool valid() const {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE get() const {
        return handle_;
    }

private:
    HANDLE handle_;
};

}  // namespace

ProcessListResult WindowsProcessManager::list_processes() const {
    AutoHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        return {false, {}, format_windows_error(GetLastError())};
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.get(), &entry) == FALSE) {
        return {false, {}, format_windows_error(GetLastError())};
    }

    std::vector<ProcessInfo> processes;
    do {
        ProcessInfo info;
        info.process_id = static_cast<unsigned long>(entry.th32ProcessID);
        info.executable_name = wstring_to_utf8(entry.szExeFile);
        processes.push_back(std::move(info));
    } while (Process32NextW(snapshot.get(), &entry) != FALSE);

    std::sort(processes.begin(), processes.end(),
              [](const ProcessInfo& left, const ProcessInfo& right) {
                  if (left.executable_name != right.executable_name) {
                      return left.executable_name < right.executable_name;
                  }
                  return left.process_id < right.process_id;
              });
    return {true, std::move(processes), ""};
}

LaunchProcessResult WindowsProcessManager::launch_process(const std::string& command_line) const {
    try {
        std::wstring command_line_w = utf8_to_wstring(command_line);
        std::vector<wchar_t> mutable_command_line(command_line_w.begin(), command_line_w.end());
        mutable_command_line.push_back(L'\0');

        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info{};

        const BOOL created =
            CreateProcessW(nullptr, mutable_command_line.data(), nullptr, nullptr, FALSE, 0,
                           nullptr, nullptr, &startup_info, &process_info);
        if (created == FALSE) {
            return {false, 0, format_windows_error(GetLastError())};
        }

        const unsigned long process_id =
            static_cast<unsigned long>(process_info.dwProcessId);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return {true, process_id, ""};
    } catch (const std::exception& error) {
        return {false, 0, error.what()};
    }
}
