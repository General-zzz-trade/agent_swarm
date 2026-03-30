#ifndef AGENT_PLUGIN_LOADER_H
#define AGENT_PLUGIN_LOADER_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "tool.h"
#include "../core/interfaces/command_runner.h"

struct PluginManifest {
    std::string name;
    std::string version;
    std::string description;
    std::filesystem::path plugin_dir;

    struct ToolDef {
        std::string name;
        std::string description;
        std::string command;        // e.g. "python3 run.py"
        ToolSchema schema;
        bool read_only = false;
    };
    std::vector<ToolDef> tools;
};

class PluginLoader {
public:
    explicit PluginLoader(std::shared_ptr<ICommandRunner> command_runner);

    // Discover and load plugins from a directory
    std::vector<std::unique_ptr<Tool>> load_plugins(const std::filesystem::path& plugin_dir);

    // Load a single plugin from its directory
    std::vector<std::unique_ptr<Tool>> load_plugin(const std::filesystem::path& plugin_path);

    // List discovered plugin manifests
    std::vector<PluginManifest> discover(const std::filesystem::path& plugin_dir);

private:
    std::shared_ptr<ICommandRunner> command_runner_;

    PluginManifest parse_manifest(const std::filesystem::path& manifest_path);
    std::unique_ptr<Tool> create_plugin_tool(const PluginManifest::ToolDef& def,
                                              const std::filesystem::path& plugin_dir);
};

#endif
