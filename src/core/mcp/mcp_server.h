#ifndef CORE_MCP_MCP_SERVER_H
#define CORE_MCP_MCP_SERVER_H

#include <functional>
#include <string>
#include <vector>

#include "../model/tool_schema.h"

/// Model Context Protocol (MCP) server implementation.
///
/// MCP is a standard protocol for LLM tool communication, used by
/// Claude Code, Cursor, and other AI coding tools. This lets Bolt's
/// tools be called by any MCP-compatible client.
///
/// Protocol: JSON-RPC 2.0 over stdin/stdout
///
/// Supported methods:
///   initialize         → capabilities and tool list
///   tools/list         → available tools with schemas
///   tools/call         → execute a tool and return result
///   shutdown           → clean exit
///
/// Usage:
///   bolt mcp-server    → starts MCP server on stdin/stdout
class McpServer {
public:
    struct ToolHandler {
        std::string name;
        std::string description;
        ToolSchema schema;
        std::function<std::string(const std::string& arguments)> execute;
    };

    void register_tool(ToolHandler handler);

    /// Run the MCP server loop (reads JSON-RPC from stdin, writes to stdout).
    int run(std::istream& input, std::ostream& output);

private:
    std::vector<ToolHandler> tools_;

    std::string handle_request(const std::string& json_request);
    std::string handle_initialize(const std::string& id);
    std::string handle_tools_list(const std::string& id);
    std::string handle_tools_call(const std::string& id, const std::string& tool_name,
                                   const std::string& arguments);
    std::string make_error(const std::string& id, int code, const std::string& message);
};

#endif
