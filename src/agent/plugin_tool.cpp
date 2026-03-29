#include "plugin_tool.h"

#include <array>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

PluginTool::PluginTool(std::string command, std::string tool_name,
                       std::string tool_description)
    : command_(std::move(command)),
      name_(std::move(tool_name)),
      description_(std::move(tool_description)) {}

std::string PluginTool::name() const { return name_; }
std::string PluginTool::description() const { return description_; }
ToolSchema PluginTool::schema() const { return schema_; }

bool PluginTool::initialize() {
    try {
        const std::string response = call_plugin(R"({"method":"describe"})");
        auto j = json::parse(response);
        name_ = j.value("name", name_);
        description_ = j.value("description", description_);

        if (j.contains("schema")) {
            schema_.name = name_;
            schema_.description = description_;
            schema_.parameters.clear();
            if (j["schema"].contains("parameters")) {
                for (auto& [key, val] : j["schema"]["parameters"].items()) {
                    schema_.parameters.push_back({
                        key,
                        val.value("type", "string"),
                        val.value("description", ""),
                        val.value("required", false)
                    });
                }
            }
        } else {
            schema_ = {name_, description_, {{"args", "string", "Tool arguments", false}}};
        }
        return true;
    } catch (...) {
        if (name_.empty()) name_ = "plugin";
        if (description_.empty()) description_ = "External plugin";
        schema_ = {name_, description_, {{"args", "string", "Tool arguments", false}}};
        return false;
    }
}

ToolResult PluginTool::run(const std::string& args) const {
    try {
        json request;
        request["method"] = "run";
        request["params"] = {{"args", args}};

        const std::string response = call_plugin(request.dump());
        auto j = json::parse(response);

        const bool success = j.value("success", true);
        const std::string result = j.value("result", response);
        return {success, result};
    } catch (const std::exception& e) {
        return {false, std::string("Plugin error: ") + e.what()};
    }
}

std::string PluginTool::call_plugin(const std::string& json_request) const {
    // Launch plugin subprocess, write request to stdin, read response from stdout
    const std::string full_command = "echo '" + json_request + "' | " + command_;

    FILE* pipe = popen(full_command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to launch plugin: " + command_);
    }

    std::string output;
    std::array<char, 4096> buffer;
    while (true) {
        const size_t n = fread(buffer.data(), 1, buffer.size(), pipe);
        if (n == 0) break;
        output.append(buffer.data(), n);
    }

    const int status = pclose(pipe);
    if (status != 0 && output.empty()) {
        throw std::runtime_error("Plugin exited with status " + std::to_string(status));
    }

    // Trim trailing whitespace
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    return output;
}
