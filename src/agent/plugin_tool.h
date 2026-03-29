#ifndef AGENT_PLUGIN_TOOL_H
#define AGENT_PLUGIN_TOOL_H

#include <string>
#include "tool.h"

/// External plugin tool: wraps a subprocess that communicates via JSON-RPC.
///
/// Plugin protocol (stdin/stdout):
///   → {"method":"describe"} ← {"name":"...","description":"...","schema":{...}}
///   → {"method":"run","params":{"args":"..."}} ← {"result":"...","success":true}
///
/// Config (bolt.conf):
///   plugins.my_tool = /path/to/plugin-binary
///   plugins.web_search = python3 /path/to/search.py
///
/// This lets users extend Bolt with tools written in any language.
class PluginTool : public Tool {
public:
    PluginTool(std::string command, std::string tool_name = "",
               std::string tool_description = "");

    std::string name() const override;
    std::string description() const override;
    ToolResult run(const std::string& args) const override;
    ToolSchema schema() const override;

    /// Call the plugin's "describe" method to get name/description/schema.
    bool initialize();

private:
    std::string command_;
    std::string name_;
    std::string description_;
    ToolSchema schema_;

    std::string call_plugin(const std::string& json_request) const;
};

#endif
