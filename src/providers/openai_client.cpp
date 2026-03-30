#include "openai_client.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "../core/net/sse_parser.h"

using json = nlohmann::json;

namespace {

json tool_schema_to_json(const ToolSchema& schema) {
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

    json func = {
        {"type", "function"},
        {"function", {
            {"name", schema.name},
            {"description", schema.description},
            {"parameters", {
                {"type", "object"},
                {"properties", properties},
                {"required", required_params}
            }}
        }}
    };
    return func;
}

json message_to_json(const ChatMessage& msg) {
    json j;
    j["role"] = chat_role_to_string(msg.role);

    if (msg.role == ChatRole::assistant && msg.has_tool_calls()) {
        if (!msg.content.empty()) {
            j["content"] = msg.content;
        } else {
            j["content"] = nullptr;
        }
        // Include reasoning_content for thinking models (kimi-k2.5, deepseek-r1)
        if (!msg.reasoning_content.empty()) {
            j["reasoning_content"] = msg.reasoning_content;
        }
        json tool_calls = json::array();
        for (const auto& tc : msg.tool_calls) {
            tool_calls.push_back({
                {"id", tc.id},
                {"type", "function"},
                {"function", {
                    {"name", tc.name},
                    {"arguments", tc.arguments}
                }}
            });
        }
        j["tool_calls"] = tool_calls;
    } else if (msg.role == ChatRole::tool) {
        j["content"] = msg.content;
        j["tool_call_id"] = msg.tool_call_id;
    } else {
        j["content"] = msg.content;
        // Include reasoning_content for thinking model assistant messages
        if (msg.role == ChatRole::assistant && !msg.reasoning_content.empty()) {
            j["reasoning_content"] = msg.reasoning_content;
        }
    }

    return j;
}

}  // namespace

OpenAiClient::OpenAiClient(OpenAiConfig config, std::shared_ptr<IHttpTransport> transport)
    : config_(std::move(config)),
      transport_(std::move(transport)) {
    if (transport_ == nullptr) {
        throw std::invalid_argument("OpenAiClient requires an HTTP transport");
    }
    if (config_.api_key.empty()) {
        throw std::invalid_argument("OpenAiClient requires an API key");
    }
}

std::string OpenAiClient::generate(const std::string& prompt) const {
    std::vector<ChatMessage> messages;
    messages.push_back({ChatRole::user, prompt});
    ChatMessage response = chat(messages, {});
    return response.content;
}

const std::string& OpenAiClient::model() const {
    return config_.model;
}

std::string OpenAiClient::build_request_body(const std::vector<ChatMessage>& messages,
                                              const std::vector<ToolSchema>& tools,
                                              bool stream) const {
    json body;
    body["model"] = config_.model;
    body["temperature"] = config_.temperature;
    body["max_tokens"] = config_.max_tokens;
    body["stream"] = stream;

    json msg_array = json::array();
    for (const auto& msg : messages) {
        msg_array.push_back(message_to_json(msg));
    }
    body["messages"] = msg_array;

    if (!tools.empty()) {
        json tools_array = json::array();
        for (const auto& tool : tools) {
            tools_array.push_back(tool_schema_to_json(tool));
        }
        body["tools"] = tools_array;
    }

    return body.dump();
}

HttpRequest OpenAiClient::make_request(const std::string& body) const {
    HttpRequest request;
    request.method = "POST";

    // Build URL: if base_url already ends with a versioned path (e.g. /v1, /v4),
    // append /chat/completions directly; otherwise prepend /v1.
    std::string base = config_.base_url;
    // Strip trailing slash
    while (!base.empty() && base.back() == '/') base.pop_back();

    // Check if base already ends with /v1, /v2, /v3, /v4, etc.
    bool has_version = false;
    if (base.size() >= 3) {
        auto last_slash = base.rfind('/');
        if (last_slash != std::string::npos) {
            std::string last_segment = base.substr(last_slash);
            if (last_segment.size() >= 3 && last_segment[0] == '/' && last_segment[1] == 'v' &&
                std::isdigit(static_cast<unsigned char>(last_segment[2]))) {
                has_version = true;
            }
        }
    }

    if (has_version) {
        request.url = base + "/chat/completions";
    } else {
        request.url = base + "/v1/chat/completions";
    }

    request.body = body;
    request.timeout_ms = config_.timeout_ms;
    request.headers = {
        {"Authorization", "Bearer " + config_.api_key},
        {"Content-Type", "application/json"}
    };
    return request;
}

ChatMessage OpenAiClient::parse_response(const std::string& body) const {
    json j = json::parse(body);

    if (j.contains("error")) {
        const std::string error_msg = j["error"].value("message", "Unknown API error");
        throw std::runtime_error("OpenAI API error: " + error_msg);
    }

    const auto& choice = j["choices"][0];
    const auto& msg = choice["message"];

    ChatMessage result;
    result.role = ChatRole::assistant;

    if (msg.contains("content") && !msg["content"].is_null()) {
        result.content = msg["content"].get<std::string>();
    }

    // Support reasoning/thinking models (Kimi K2.5, DeepSeek R1, o3, etc.)
    // Preserve reasoning_content for re-sending in multi-turn conversations.
    if (msg.contains("reasoning_content") && !msg["reasoning_content"].is_null()) {
        result.reasoning_content = msg["reasoning_content"].get<std::string>();
    }
    // When content is empty, use reasoning as the reply text.
    if (result.content.empty() && !result.reasoning_content.empty()) {
        result.content = result.reasoning_content;
    }

    if (msg.contains("tool_calls")) {
        for (const auto& tc : msg["tool_calls"]) {
            ToolCallRequest call;
            call.id = tc["id"].get<std::string>();
            call.name = tc["function"]["name"].get<std::string>();
            call.arguments = tc["function"]["arguments"].get<std::string>();
            result.tool_calls.push_back(std::move(call));
        }
    }

    // Extract token usage
    try {
        if (j.contains("usage")) {
            const auto& usage = j["usage"];
            result.usage.input_tokens = usage.value("prompt_tokens", 0);
            result.usage.output_tokens = usage.value("completion_tokens", 0);
        }
    } catch (...) {}

    return result;
}

ChatMessage OpenAiClient::chat(const std::vector<ChatMessage>& messages,
                                const std::vector<ToolSchema>& tools) const {
    const std::string body = build_request_body(messages, tools, false);
    HttpRequest request = make_request(body);

    const HttpResponse response = transport_->send(request);

    if (!response.error.empty()) {
        throw std::runtime_error("OpenAI request failed: " + response.error);
    }
    if (response.status_code != 200) {
        std::string detail = response.body;
        try {
            json err = json::parse(response.body);
            if (err.contains("error")) {
                detail = err["error"].value("message", response.body);
            }
        } catch (...) {}
        throw std::runtime_error("OpenAI API returned HTTP " +
                                 std::to_string(response.status_code) + ": " + detail);
    }

    return parse_response(response.body);
}

ChatMessage OpenAiClient::chat_streaming(const std::vector<ChatMessage>& messages,
                                          const std::vector<ToolSchema>& tools,
                                          TokenCallback on_token) const {
    const std::string body = build_request_body(messages, tools, true);
    HttpRequest request = make_request(body);

    ChatMessage result;
    result.role = ChatRole::assistant;

    // Track tool call deltas by index
    struct ToolCallAccumulator {
        std::string id;
        std::string name;
        std::string arguments;
    };
    std::vector<ToolCallAccumulator> tool_accumulators;

    SseParser parser([&](const SseParser::Event& event) -> bool {
        if (event.data == "[DONE]") {
            return false;
        }

        try {
            json j = json::parse(event.data);
            const auto& delta = j["choices"][0]["delta"];

            if (delta.contains("content") && !delta["content"].is_null()) {
                const std::string token = delta["content"].get<std::string>();
                result.content += token;
                if (on_token && !token.empty()) {
                    if (!on_token(token)) {
                        return false;
                    }
                }
            }

            if (delta.contains("tool_calls")) {
                for (const auto& tc_delta : delta["tool_calls"]) {
                    const int index = tc_delta.value("index", 0);

                    while (static_cast<int>(tool_accumulators.size()) <= index) {
                        tool_accumulators.push_back({});
                    }

                    auto& acc = tool_accumulators[index];
                    if (tc_delta.contains("id")) {
                        acc.id = tc_delta["id"].get<std::string>();
                    }
                    if (tc_delta.contains("function")) {
                        const auto& func = tc_delta["function"];
                        if (func.contains("name")) {
                            acc.name += func["name"].get<std::string>();
                        }
                        if (func.contains("arguments")) {
                            acc.arguments += func["arguments"].get<std::string>();
                        }
                    }
                }
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
        throw std::runtime_error("OpenAI streaming request failed: " + response.error);
    }

    // Convert accumulated tool calls
    for (const auto& acc : tool_accumulators) {
        if (!acc.name.empty()) {
            result.tool_calls.push_back({acc.id, acc.name, acc.arguments});
        }
    }

    return result;
}
