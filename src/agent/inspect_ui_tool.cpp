#include "inspect_ui_tool.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "workspace_utils.h"

namespace {

InspectUiRequest parse_request(const std::string& raw_args) {
    InspectUiRequest request;
    std::istringstream input(raw_args);
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = trim_copy(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.rfind("handle=", 0) == 0 || trimmed.rfind("handle:", 0) == 0 ||
            trimmed.rfind("window_handle=", 0) == 0 || trimmed.rfind("window_handle:", 0) == 0) {
            const std::size_t offset = trimmed[0] == 'h' ? 7 : 14;
            request.window_handle = trim_copy(trimmed.substr(offset));
            continue;
        }
        if (trimmed.rfind("max_elements=", 0) == 0 || trimmed.rfind("max_elements:", 0) == 0) {
            const std::string value = trim_copy(trimmed.substr(13));
            try {
                request.max_elements = static_cast<std::size_t>(std::stoull(value));
            } catch (const std::exception&) {
                throw std::runtime_error("inspect_ui max_elements must be a positive integer");
            }
            continue;
        }
        throw std::runtime_error("inspect_ui received an unexpected argument: " + trimmed);
    }
    return request;
}

}  // namespace

InspectUiTool::InspectUiTool(std::shared_ptr<IUiAutomation> ui_automation)
    : ui_automation_(std::move(ui_automation)) {
    if (ui_automation_ == nullptr) {
        throw std::invalid_argument("InspectUiTool requires UI automation support");
    }
}

std::string InspectUiTool::name() const {
    return "inspect_ui";
}

std::string InspectUiTool::description() const {
    return "Inspect the foreground window or a specific window handle and list visible child elements.";
}

ToolPreview InspectUiTool::preview(const std::string& args) const {
    try {
        const InspectUiRequest request = parse_request(args);
        std::ostringstream details;
        details << "max_elements=" << request.max_elements;
        if (!request.window_handle.empty()) {
            details << "\nwindow_handle=" << request.window_handle;
        }
        return {"Inspect window UI tree", details.str()};
    } catch (const std::exception& error) {
        return {"Unable to preview inspect_ui request", error.what()};
    }
}

ToolResult InspectUiTool::run(const std::string& args) const {
    try {
        const InspectUiRequest request = parse_request(args);
        const InspectUiResult result = ui_automation_->inspect_ui(request);
        if (!result.success) {
            return {false, result.error};
        }

        std::ostringstream output;
        output << "WINDOW_HANDLE: " << result.window_handle << "\n";
        output << "WINDOW_TITLE: " << result.window_title << "\n";
        output << "ELEMENTS: " << result.elements.size() << "\n";

        const std::size_t rows = std::min(result.elements.size(), request.max_elements);
        for (std::size_t i = 0; i < rows; ++i) {
            const UiElementInfo& element = result.elements[i];
            output << element.handle << " parent=" << element.parent_handle
                   << " class=" << element.class_name
                   << " visible=" << (element.visible ? "true" : "false")
                   << " enabled=" << (element.enabled ? "true" : "false")
                   << " text=" << element.text << "\n";
        }
        if (result.elements.size() > rows) {
            output << "[truncated " << (result.elements.size() - rows) << " elements]\n";
        }

        return {true, output.str()};
    } catch (const std::exception& error) {
        return {false, error.what()};
    }
}
