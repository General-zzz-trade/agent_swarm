#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "../src/core/mcp/mcp_server.h"
#include "../src/core/model/tool_schema.h"

using json = nlohmann::json;

namespace {

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_equal(const std::string& actual, const std::string& expected,
                  const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + " (expected '" + expected + "', got '" + actual + "')");
    }
}

void expect_equal(int actual, int expected, const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + " (expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual) + ")");
    }
}

McpServer make_server_with_echo_tool() {
    McpServer server;
    McpServer::ToolHandler handler;
    handler.name = "echo";
    handler.description = "Echoes the input";
    handler.schema.name = "echo";
    handler.schema.description = "Echoes the input";
    handler.schema.parameters = {{"text", "string", "The text to echo", true}};
    handler.execute = [](const std::string& arguments) -> std::string {
        // MCP server may pass raw JSON or extracted string value
        try {
            auto args = json::parse(arguments);
            if (args.is_object()) return args.value("text", arguments);
            return arguments;
        } catch (...) {
            return arguments;  // Plain text after arg conversion
        }
    };
    server.register_tool(std::move(handler));
    return server;
}

std::string send_request(McpServer& server, const std::string& request) {
    std::istringstream input(request + "\n");
    std::ostringstream output;
    server.run(input, output);
    return output.str();
}

// Parse the first line of output as JSON
json parse_response(const std::string& output) {
    std::istringstream stream(output);
    std::string line;
    std::getline(stream, line);
    return json::parse(line);
}

// --- Tests ---

void expect_initialize_returns_protocol_version_and_capabilities() {
    McpServer server;
    std::string request = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    // Send initialize followed by shutdown to exit the run loop
    std::istringstream input(request + "\n" + R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})" + "\n");
    std::ostringstream output;
    server.run(input, output);

    // Parse first response line (initialize response)
    std::istringstream out_stream(output.str());
    std::string first_line;
    std::getline(out_stream, first_line);
    auto resp = json::parse(first_line);

    expect_equal(resp["jsonrpc"].get<std::string>(), "2.0", "jsonrpc version");
    expect_equal(resp["result"]["protocolVersion"].get<std::string>(), "2024-11-05",
                 "protocol version");
    expect_true(resp["result"].contains("capabilities"), "should have capabilities");
    expect_true(resp["result"]["capabilities"].contains("tools"), "should have tools capability");
    expect_equal(resp["result"]["serverInfo"]["name"].get<std::string>(), "bolt",
                 "server name");
}

void expect_tools_list_returns_registered_tools() {
    auto server = make_server_with_echo_tool();
    std::string request = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    std::istringstream input(request + "\n" + R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})" + "\n");
    std::ostringstream output;
    server.run(input, output);

    std::istringstream out_stream(output.str());
    std::string first_line;
    std::getline(out_stream, first_line);
    auto resp = json::parse(first_line);

    auto tools = resp["result"]["tools"];
    expect_equal(static_cast<int>(tools.size()), 1, "should have one tool");
    expect_equal(tools[0]["name"].get<std::string>(), "echo", "tool name");
    expect_equal(tools[0]["description"].get<std::string>(), "Echoes the input",
                 "tool description");
    expect_true(tools[0].contains("inputSchema"), "tool should have inputSchema");
    expect_true(tools[0]["inputSchema"]["properties"].contains("text"),
                "schema should have text property");
}

void expect_tools_call_executes_tool() {
    auto server = make_server_with_echo_tool();
    std::string request = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"echo","arguments":{"text":"hello"}}})";
    std::istringstream input(request + "\n" + R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})" + "\n");
    std::ostringstream output;
    server.run(input, output);

    std::istringstream out_stream(output.str());
    std::string first_line;
    std::getline(out_stream, first_line);
    auto resp = json::parse(first_line);

    expect_true(resp.contains("result"), "should have result");
    auto content = resp["result"]["content"];
    expect_equal(static_cast<int>(content.size()), 1, "should have one content item");
    expect_equal(content[0]["type"].get<std::string>(), "text", "content type");
    // MCP server passes raw JSON args for unknown tool patterns;
    // the echo mock receives the single-value extraction result
    const std::string echoed = content[0]["text"].get<std::string>();
    expect_true(echoed == "hello" || echoed.find("hello") != std::string::npos,
                "echoed text should contain hello");
}

void expect_tools_call_unknown_tool_returns_error() {
    auto server = make_server_with_echo_tool();
    std::string request = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nonexistent","arguments":{}}})";
    std::istringstream input(request + "\n" + R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})" + "\n");
    std::ostringstream output;
    server.run(input, output);

    std::istringstream out_stream(output.str());
    std::string first_line;
    std::getline(out_stream, first_line);
    auto resp = json::parse(first_line);

    expect_true(resp.contains("error"), "should have error");
    expect_equal(resp["error"]["code"].get<int>(), -32602, "error code for unknown tool");
    expect_true(resp["error"]["message"].get<std::string>().find("nonexistent") != std::string::npos,
                "error message should mention tool name");
}

void expect_unknown_method_returns_32601() {
    McpServer server;
    std::string request = R"({"jsonrpc":"2.0","id":1,"method":"unknown/method","params":{}})";
    std::istringstream input(request + "\n" + R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})" + "\n");
    std::ostringstream output;
    server.run(input, output);

    std::istringstream out_stream(output.str());
    std::string first_line;
    std::getline(out_stream, first_line);
    auto resp = json::parse(first_line);

    expect_true(resp.contains("error"), "should have error");
    expect_equal(resp["error"]["code"].get<int>(), -32601, "method not found error code");
}

void expect_malformed_json_returns_parse_error() {
    McpServer server;
    std::string request = "this is not json";
    std::istringstream input(request + "\n" + R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})" + "\n");
    std::ostringstream output;
    server.run(input, output);

    std::istringstream out_stream(output.str());
    std::string first_line;
    std::getline(out_stream, first_line);
    auto resp = json::parse(first_line);

    expect_true(resp.contains("error"), "should have error");
    expect_equal(resp["error"]["code"].get<int>(), -32700, "parse error code");
}

void expect_notification_returns_empty() {
    McpServer server;
    // A notification has no "id" field
    std::string request = R"({"jsonrpc":"2.0","method":"notifications/initialized","params":{}})";
    std::istringstream input(request + "\n" + R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})" + "\n");
    std::ostringstream output;
    server.run(input, output);

    // The first line should be the shutdown response (notification produces no output)
    std::istringstream out_stream(output.str());
    std::string first_line;
    std::getline(out_stream, first_line);
    auto resp = json::parse(first_line);

    // This should be the shutdown response, not a notification response
    expect_equal(resp["id"].get<int>(), 2, "first response should be shutdown");
    expect_true(resp.contains("result"), "shutdown should have result");
}

void expect_shutdown_returns_result() {
    McpServer server;
    std::string request = R"({"jsonrpc":"2.0","id":1,"method":"shutdown"})";
    std::istringstream input(request + "\n");
    std::ostringstream output;
    server.run(input, output);

    std::istringstream out_stream(output.str());
    std::string first_line;
    std::getline(out_stream, first_line);
    auto resp = json::parse(first_line);

    expect_equal(resp["jsonrpc"].get<std::string>(), "2.0", "jsonrpc version");
    expect_equal(resp["id"].get<int>(), 1, "response id");
    expect_true(resp.contains("result"), "shutdown should have result");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"initialize returns protocol version and capabilities",
         expect_initialize_returns_protocol_version_and_capabilities},
        {"tools/list returns registered tools with schemas",
         expect_tools_list_returns_registered_tools},
        {"tools/call executes tool and returns result",
         expect_tools_call_executes_tool},
        {"tools/call with unknown tool returns error",
         expect_tools_call_unknown_tool_returns_error},
        {"unknown method returns -32601 error",
         expect_unknown_method_returns_32601},
        {"malformed JSON returns -32700 parse error",
         expect_malformed_json_returns_parse_error},
        {"notification returns empty string",
         expect_notification_returns_empty},
        {"shutdown returns result",
         expect_shutdown_returns_result},
    };

    std::size_t passed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            ++passed;
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << "Passed " << passed << " MCP server tests.\n";
    return 0;
}
