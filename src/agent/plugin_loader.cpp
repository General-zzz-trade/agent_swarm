#include "plugin_loader.h"

#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// PluginExternalTool - wraps an external command as a Tool
class PluginExternalTool : public Tool {
public:
    PluginExternalTool(std::string name, std::string description,
                        std::string command, std::filesystem::path plugin_dir,
                        ToolSchema schema, bool read_only,
                        std::shared_ptr<ICommandRunner> runner)
        : name_(std::move(name)), description_(std::move(description)),
          command_(std::move(command)), plugin_dir_(std::move(plugin_dir)),
          schema_(std::move(schema)), read_only_(read_only),
          runner_(std::move(runner)) {}

    std::string name() const override { return name_; }
    std::string description() const override { return description_; }
    ToolSchema schema() const override { return schema_; }
    bool is_read_only() const override { return read_only_; }

    ToolResult run(const std::string& args) const override {
        // Pass args as JSON via stdin using echo pipe
        std::string escaped_args;
        for (char c : args) {
            if (c == '\'') {
                escaped_args += "'\\''";
            } else {
                escaped_args += c;
            }
        }

        const std::string full_command =
            "echo '" + escaped_args + "' | " + command_;

        auto result = runner_->run(full_command, plugin_dir_, 30000);

        if (result.success) {
            return {true, result.stdout_output};
        }

        std::string error = result.stderr_output;
        if (error.empty()) {
            error = "Plugin exited with code " + std::to_string(result.exit_code);
        }
        return {false, error};
    }

private:
    std::string name_;
    std::string description_;
    std::string command_;
    std::filesystem::path plugin_dir_;
    ToolSchema schema_;
    bool read_only_;
    std::shared_ptr<ICommandRunner> runner_;
};

// Implementation of PluginLoader

PluginLoader::PluginLoader(std::shared_ptr<ICommandRunner> command_runner)
    : command_runner_(std::move(command_runner)) {}

PluginManifest PluginLoader::parse_manifest(const std::filesystem::path& manifest_path) {
    PluginManifest manifest;
    manifest.plugin_dir = manifest_path.parent_path();

    std::ifstream f(manifest_path);
    if (!f) {
        throw std::runtime_error("Cannot open " + manifest_path.string());
    }

    auto j = json::parse(f);
    manifest.name = j.value("name", "");
    manifest.version = j.value("version", "1.0.0");
    manifest.description = j.value("description", "");

    if (j.contains("tools") && j["tools"].is_array()) {
        for (const auto& tool_json : j["tools"]) {
            PluginManifest::ToolDef def;
            def.name = tool_json.value("name", "");
            def.description = tool_json.value("description", "");
            def.command = tool_json.value("command", "");
            def.read_only = tool_json.value("read_only", false);

            // Parse schema
            if (tool_json.contains("schema")) {
                const auto& s = tool_json["schema"];
                if (s.contains("properties") && s["properties"].is_object()) {
                    for (auto it = s["properties"].begin(); it != s["properties"].end(); ++it) {
                        ToolParameter param;
                        param.name = it.key();
                        param.type = it->value("type", "string");
                        param.description = it->value("description", "");
                        def.schema.parameters.push_back(param);
                    }
                }
                if (s.contains("required") && s["required"].is_array()) {
                    for (const auto& r : s["required"]) {
                        for (auto& p : def.schema.parameters) {
                            if (p.name == r.get<std::string>()) {
                                p.required = true;
                            }
                        }
                    }
                }
            }

            def.schema.name = def.name;
            def.schema.description = def.description;

            if (!def.name.empty() && !def.command.empty()) {
                manifest.tools.push_back(std::move(def));
            }
        }
    }

    return manifest;
}

std::unique_ptr<Tool> PluginLoader::create_plugin_tool(
    const PluginManifest::ToolDef& def,
    const std::filesystem::path& plugin_dir) {
    return std::make_unique<PluginExternalTool>(
        def.name, def.description, def.command,
        plugin_dir, def.schema, def.read_only, command_runner_);
}

std::vector<PluginManifest> PluginLoader::discover(const std::filesystem::path& plugin_dir) {
    std::vector<PluginManifest> manifests;

    if (!std::filesystem::exists(plugin_dir)) return manifests;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(plugin_dir, ec)) {
        if (!entry.is_directory()) continue;

        auto manifest_path = entry.path() / "plugin.json";
        if (!std::filesystem::exists(manifest_path)) continue;

        try {
            manifests.push_back(parse_manifest(manifest_path));
        } catch (...) {
            // Skip invalid plugins
        }
    }

    return manifests;
}

std::vector<std::unique_ptr<Tool>> PluginLoader::load_plugin(
    const std::filesystem::path& plugin_path) {
    std::vector<std::unique_ptr<Tool>> tools;

    auto manifest_path = plugin_path / "plugin.json";
    if (!std::filesystem::exists(manifest_path)) return tools;

    try {
        auto manifest = parse_manifest(manifest_path);
        for (const auto& def : manifest.tools) {
            tools.push_back(create_plugin_tool(def, manifest.plugin_dir));
        }
    } catch (...) {}

    return tools;
}

std::vector<std::unique_ptr<Tool>> PluginLoader::load_plugins(
    const std::filesystem::path& plugin_dir) {
    std::vector<std::unique_ptr<Tool>> all_tools;

    auto manifests = discover(plugin_dir);
    for (const auto& manifest : manifests) {
        for (const auto& def : manifest.tools) {
            all_tools.push_back(create_plugin_tool(def, manifest.plugin_dir));
        }
    }

    return all_tools;
}
