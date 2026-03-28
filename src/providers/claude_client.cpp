#include "claude_client.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "../core/net/sse_parser.h"

using json = nlohmann::json;

namespace {

json tool_schema_to_claude(const ToolSchema& schema, bool cache) {
    json properties = json::object();
    std::vector<std::string> required_params;

    for (const auto& param : schema.parameters) {
        properties[param.name] = {
            {"type", param.type},
            {"description", param.description}
        };
        if (param.required) {
            required_params.push_back(param.name);
        }
    }

    json tool = {
        {"name", schema.name},
        {"description", schema.description},
        {"input_schema", {
            {"type", "object"},
            {"properties", properties},
            {"required", required_params}
        }}
    };

    if (cache) {
        tool["cache_control"] = {{"type", "ephemeral"}};
    }

    return tool;
}

json message_to_claude(const ChatMessage& msg) {
    json j;

    if (msg.role == ChatRole::assistant) {
        j["role"] = "assistant";
        json content = json::array();

        if (!msg.content.empty()) {
            content.push_back({{"type", "text"}, {"text", msg.content}});
        }

        for (const auto& tc : msg.tool_calls) {
            // Parse the arguments string as JSON
            json input;
            try {
                input = json::parse(tc.arguments);
            } catch (...) {
                input = {{"args", tc.arguments}};
            }

            content.push_back({
                {"type", "tool_use"},
                {"id", tc.id},
                {"name", tc.name},
                {"input", input}
            });
        }

        j["content"] = content;
    } else if (msg.role == ChatRole::tool) {
        // Tool results go as user messages with tool_result content blocks
        j["role"] = "user";
        j["content"] = json::array({
            {
                {"type", "tool_result"},
                {"tool_use_id", msg.tool_call_id},
                {"content", msg.content}
            }
        });
    } else {
        j["role"] = "user";
        j["content"] = msg.content;
    }

    return j;
}

}  // namespace

ClaudeClient::ClaudeClient(ClaudeConfig config, std::shared_ptr<IHttpTransport> transport)
    : config_(std::move(config)),
      transport_(std::move(transport)) {
    if (transport_ == nullptr) {
        throw std::invalid_argument("ClaudeClient requires an HTTP transport");
    }
    if (config_.api_key.empty()) {
        throw std::invalid_argument("ClaudeClient requires an API key");
    }
}

std::string ClaudeClient::generate(const std::string& prompt) const {
    std::vector<ChatMessage> messages;
    messages.push_back({ChatRole::user, prompt});
    ChatMessage response = chat(messages, {});
    return response.content;
}

const std::string& ClaudeClient::model() const {
    return config_.model;
}

std::string ClaudeClient::build_request_body(const std::vector<ChatMessage>& messages,
                                              const std::vector<ToolSchema>& tools,
                                              bool stream) const {
    json body;
    body["model"] = config_.model;
    body["max_tokens"] = config_.max_tokens;
    body["temperature"] = config_.temperature;
    body["stream"] = stream;

    // Extract system message
    std::string system_content;
    for (const auto& msg : messages) {
        if (msg.role == ChatRole::system) {
            system_content += msg.content + "\n";
        }
    }
    if (!system_content.empty()) {
        if (config_.enable_prompt_caching) {
            body["system"] = json::array({
                {
                    {"type", "text"},
                    {"text", system_content},
                    {"cache_control", {{"type", "ephemeral"}}}
                }
            });
        } else {
            body["system"] = system_content;
        }
    }

    // Build messages array (skip system messages)
    json msg_array = json::array();
    for (const auto& msg : messages) {
        if (msg.role == ChatRole::system) {
            continue;
        }
        msg_array.push_back(message_to_claude(msg));
    }
    body["messages"] = msg_array;

    // Tools
    if (!tools.empty()) {
        json tools_array = json::array();
        for (std::size_t i = 0; i < tools.size(); ++i) {
            bool cache = config_.enable_prompt_caching && (i == tools.size() - 1);
            tools_array.push_back(tool_schema_to_claude(tools[i], cache));
        }
        body["tools"] = tools_array;
    }

    return body.dump();
}

HttpRequest ClaudeClient::make_request(const std::string& body) const {
    HttpRequest request;
    request.method = "POST";
    request.url = config_.base_url + "/v1/messages";
    request.body = body;
    request.timeout_ms = config_.timeout_ms;
    request.headers = {
        {"x-api-key", config_.api_key},
        {"anthropic-version", config_.api_version}
    };
    return request;
}

ChatMessage ClaudeClient::parse_response(const std::string& body) const {
    json j = json::parse(body);

    if (j.value("type", "") == "error") {
        const std::string error_msg = j["error"].value("message", "Unknown API error");
        throw std::runtime_error("Claude API error: " + error_msg);
    }

    ChatMessage result;
    result.role = ChatRole::assistant;

    for (const auto& block : j["content"]) {
        const std::string type = block.value("type", "");
        if (type == "text") {
            result.content += block["text"].get<std::string>();
        } else if (type == "tool_use") {
            ToolCallRequest call;
            call.id = block["id"].get<std::string>();
            call.name = block["name"].get<std::string>();
            call.arguments = block["input"].dump();
            result.tool_calls.push_back(std::move(call));
        }
    }

    return result;
}

ChatMessage ClaudeClient::chat(const std::vector<ChatMessage>& messages,
                                const std::vector<ToolSchema>& tools) const {
    const std::string body = build_request_body(messages, tools, false);
    HttpRequest request = make_request(body);

    const HttpResponse response = transport_->send(request);

    if (!response.error.empty()) {
        throw std::runtime_error("Claude request failed: " + response.error);
    }
    if (response.status_code != 200) {
        std::string detail = response.body;
        try {
            json err = json::parse(response.body);
            if (err.contains("error")) {
                detail = err["error"].value("message", response.body);
            }
        } catch (...) {}
        throw std::runtime_error("Claude API returned HTTP " +
                                 std::to_string(response.status_code) + ": " + detail);
    }

    return parse_response(response.body);
}

ChatMessage ClaudeClient::chat_streaming(const std::vector<ChatMessage>& messages,
                                          const std::vector<ToolSchema>& tools,
                                          TokenCallback on_token) const {
    const std::string body = build_request_body(messages, tools, true);
    HttpRequest request = make_request(body);

    ChatMessage result;
    result.role = ChatRole::assistant;

    // Track current tool_use block being built
    std::string current_tool_id;
    std::string current_tool_name;
    std::string current_tool_input;

    SseParser parser([&](const SseParser::Event& event) -> bool {
        try {
            json j = json::parse(event.data);
            const std::string type = j.value("type", "");

            if (type == "content_block_start") {
                const auto& block = j["content_block"];
                if (block.value("type", "") == "tool_use") {
                    current_tool_id = block.value("id", "");
                    current_tool_name = block.value("name", "");
                    current_tool_input.clear();
                }
            } else if (type == "content_block_delta") {
                const auto& delta = j["delta"];
                const std::string delta_type = delta.value("type", "");

                if (delta_type == "text_delta") {
                    const std::string text = delta.value("text", "");
                    result.content += text;
                    if (on_token && !text.empty()) {
                        if (!on_token(text)) {
                            return false;
                        }
                    }
                } else if (delta_type == "input_json_delta") {
                    current_tool_input += delta.value("partial_json", "");
                }
            } else if (type == "content_block_stop") {
                if (!current_tool_name.empty()) {
                    result.tool_calls.push_back({
                        current_tool_id, current_tool_name, current_tool_input});
                    current_tool_id.clear();
                    current_tool_name.clear();
                    current_tool_input.clear();
                }
            } else if (type == "message_stop") {
                return false;
            }
        } catch (...) {
            // Skip malformed SSE events
        }

        return true;
    });

    const HttpResponse response = transport_->send_streaming(request,
        [&parser](const std::string& chunk) -> bool {
            return parser.feed(chunk);
        });
    parser.finish();

    if (!response.error.empty()) {
        throw std::runtime_error("Claude streaming request failed: " + response.error);
    }

    return result;
}
