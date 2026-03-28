#include "wait_for_window_tool.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "workspace_utils.h"

namespace {

struct WaitWindowRequest {
    std::string handle;
    std::string title;
    std::size_t timeout_ms = 5000;
};

std::string to_lower_copy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

WaitWindowRequest parse_wait_request(const std::string& raw_args) {
    WaitWindowRequest request;
    std::istringstream input(raw_args);
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = trim_copy(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.rfind("handle=", 0) == 0 || trimmed.rfind("handle:", 0) == 0) {
            request.handle = trim_copy(trimmed.substr(7));
            continue;
        }
        if (trimmed.rfind("title=", 0) == 0 || trimmed.rfind("title:", 0) == 0) {
            request.title = trim_copy(trimmed.substr(6));
            continue;
        }
        if (trimmed.rfind("timeout_ms=", 0) == 0 || trimmed.rfind("timeout_ms:", 0) == 0) {
            const std::string value = trim_copy(trimmed.substr(11));
            try {
                request.timeout_ms = static_cast<std::size_t>(std::stoull(value));
            } catch (const std::exception&) {
                throw std::runtime_error("wait_for_window timeout_ms must be a positive integer");
            }
            continue;
        }
        if (request.title.empty()) {
            request.title = trimmed;
            continue;
        }
        throw std::runtime_error("wait_for_window received an unexpected argument: " + trimmed);
    }

    if (request.handle.empty() && request.title.empty()) {
        throw std::runtime_error("wait_for_window requires a title or handle");
    }
    return request;
}

bool matches_request(const WindowInfo& info, const WaitWindowRequest& request) {
    if (!request.handle.empty()) {
        return info.handle == request.handle;
    }
    return to_lower_copy(info.title).find(to_lower_copy(request.title)) != std::string::npos;
}

std::string request_summary(const WaitWindowRequest& request) {
    std::ostringstream output;
    if (!request.handle.empty()) {
        output << "handle=" << request.handle;
    } else {
        output << "title~" << request.title;
    }
    output << ", timeout_ms=" << request.timeout_ms;
    return output.str();
}

}  // namespace

WaitForWindowTool::WaitForWindowTool(std::shared_ptr<IWindowController> window_controller)
    : window_controller_(std::move(window_controller)) {
    if (window_controller_ == nullptr) {
        throw std::invalid_argument("WaitForWindowTool requires a window controller");
    }
}

std::string WaitForWindowTool::name() const {
    return "wait_for_window";
}

std::string WaitForWindowTool::description() const {
    return "Wait until a visible top-level window appears by substring title or exact handle.";
}

ToolPreview WaitForWindowTool::preview(const std::string& args) const {
    try {
        const WaitWindowRequest request = parse_wait_request(args);
        return {"Wait for window", request_summary(request)};
    } catch (const std::exception& error) {
        return {"Unable to preview wait_for_window request", error.what()};
    }
}

ToolResult WaitForWindowTool::run(const std::string& args) const {
    try {
        const WaitWindowRequest request = parse_wait_request(args);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(request.timeout_ms);

        while (std::chrono::steady_clock::now() <= deadline) {
            const WindowListResult windows = window_controller_->list_windows();
            if (!windows.success) {
                return {false, windows.error};
            }

            for (const WindowInfo& info : windows.windows) {
                if (!matches_request(info, request)) {
                    continue;
                }

                std::ostringstream output;
                output << "FOUND WINDOW\n";
                output << "handle: " << info.handle << "\n";
                output << "pid: " << info.process_id << "\n";
                output << "class: " << info.class_name << "\n";
                output << "title: " << info.title;
                return {true, output.str()};
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        return {false, "Timed out waiting for window: " + request_summary(request)};
    } catch (const std::exception& error) {
        return {false, error.what()};
    }
}
