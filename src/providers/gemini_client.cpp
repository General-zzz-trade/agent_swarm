#include "gemini_client.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "../core/net/sse_parser.h"

using json = nlohmann::json;

namespace {

json tool_schema_to_gemini(const ToolSchema& schema) {
    json properties = json::object();
    std::vector<std::string> required_params;

    for (const auto& param : schema.parameters) {
        properties[param.name] = {
            {"type", param.type == "string" ? "STRING" : param.type},
            {"description", param.description}
        };
        if (param.required) {
            required_params.push_back(param.name);
        }
    }

    json func = {
        {"name", schema.name},
        {"description", schema.description},
        {"parameters", {
            {"type", "OBJECT"},
            {"properties", properties},
            {"required", required_params}
        }}
    };
    return func;
}

std::string gemini_role(ChatRole role) {
    switch (role) {
        case ChatRole::user: return "user";
        case ChatRole::assistant: return "model";
        case ChatRole::system: return "user";
        case ChatRole::tool: return "user";
    }
    return "user";
}

}  // namespace

GeminiClient::GeminiClient(GeminiConfig config, std::shared_ptr<IHttpTransport> transport)
    : config_(std::move(config)),
      transport_(std::move(transport)) {
    if (transport_ == nullptr) {
        throw std::invalid_argument("GeminiClient requires an HTTP transport");
    }
    if (config_.api_key.empty()) {
        throw std::invalid_argument("GeminiClient requires an API key");
    }
}

std::string GeminiClient::generate(const std::string& prompt) const {
    std::vector<ChatMessage> messages;
    messages.push_back({ChatRole::user, prompt});
    ChatMessage response = chat(messages, {});
    return response.content;
}

const std::string& GeminiClient::model() const {
    return config_.model;
}

std::string GeminiClient::build_request_body(const std::vector<ChatMessage>& messages,
                                              const std::vector<ToolSchema>& tools) const {
    json body;

    // System instruction
    std::string system_text;
    for (const auto& msg : messages) {
        if (msg.role == ChatRole::system) {
            system_text += msg.content + "\n";
        }
    }
    if (!system_text.empty()) {
        body["system_instruction"] = {
            {"parts", json::array({{{"text", system_text}}})}
        };
    }

    // Contents
    json contents = json::array();
    for (const auto& msg : messages) {
        if (msg.role == ChatRole::system) {
            continue;
        }

        json parts = json::array();

        if (msg.role == ChatRole::tool) {
            // Function response
            parts.push_back({
                {"functionResponse", {
                    {"name", msg.name},
                    {"response", {{"result", msg.content}}}
                }}
            });
            contents.push_back({{"role", "function"}, {"parts", parts}});
        } else if (msg.role == ChatRole::assistant && msg.has_tool_calls()) {
            // Model with function calls
            if (!msg.content.empty()) {
                parts.push_back({{"text", msg.content}});
            }
            for (const auto& tc : msg.tool_calls) {
                json args;
                try {
                    args = json::parse(tc.arguments);
                } catch (...) {
                    args = {{"args", tc.arguments}};
                }
                parts.push_back({
                    {"functionCall", {
                        {"name", tc.name},
                        {"args", args}
                    }}
                });
            }
            contents.push_back({{"role", "model"}, {"parts", parts}});
        } else {
            parts.push_back({{"text", msg.content}});
            contents.push_back({{"role", gemini_role(msg.role)}, {"parts", parts}});
        }
    }
    body["contents"] = contents;

    // Tools
    if (!tools.empty()) {
        json func_decls = json::array();
        for (const auto& tool : tools) {
            func_decls.push_back(tool_schema_to_gemini(tool));
        }
        body["tools"] = json::array({{{"function_declarations", func_decls}}});
    }

    // Generation config
    body["generationConfig"] = {
        {"temperature", config_.temperature},
        {"maxOutputTokens", config_.max_tokens}
    };

    return body.dump();
}

HttpRequest GeminiClient::make_request(const std::string& body, bool stream) const {
    HttpRequest request;
    request.method = "POST";

    const std::string action = stream ? "streamGenerateContent?alt=sse" : "generateContent";
    request.url = config_.base_url + "/v1beta/models/" + config_.model + ":" + action;

    request.body = body;
    request.timeout_ms = config_.timeout_ms;
    request.headers = {
        {"x-goog-api-key", config_.api_key}
    };
    return request;
}

ChatMessage GeminiClient::parse_response(const std::string& body) const {
    json j = json::parse(body);

    if (j.contains("error")) {
        const std::string error_msg = j["error"].value("message", "Unknown API error");
        throw std::runtime_error("Gemini API error: " + error_msg);
    }

    ChatMessage result;
    result.role = ChatRole::assistant;

    const auto& candidate = j["candidates"][0];
    const auto& parts = candidate["content"]["parts"];

    for (const auto& part : parts) {
        if (part.contains("text")) {
            result.content += part["text"].get<std::string>();
        } else if (part.contains("functionCall")) {
            ToolCallRequest call;
            call.name = part["functionCall"]["name"].get<std::string>();
            call.arguments = part["functionCall"]["args"].dump();
            call.id = "gemini_" + call.name;  // Gemini doesn't provide IDs
            result.tool_calls.push_back(std::move(call));
        }
    }

    return result;
}

ChatMessage GeminiClient::chat(const std::vector<ChatMessage>& messages,
                                const std::vector<ToolSchema>& tools) const {
    const std::string body = build_request_body(messages, tools);
    HttpRequest request = make_request(body, false);

    const HttpResponse response = transport_->send(request);

    if (!response.error.empty()) {
        throw std::runtime_error("Gemini request failed: " + response.error);
    }
    if (response.status_code != 200) {
        std::string detail = response.body;
        try {
            json err = json::parse(response.body);
            if (err.contains("error")) {
                detail = err["error"].value("message", response.body);
            }
        } catch (...) {}
        throw std::runtime_error("Gemini API returned HTTP " +
                                 std::to_string(response.status_code) + ": " + detail);
    }

    return parse_response(response.body);
}

ChatMessage GeminiClient::chat_streaming(const std::vector<ChatMessage>& messages,
                                          const std::vector<ToolSchema>& tools,
                                          TokenCallback on_token) const {
    const std::string body = build_request_body(messages, tools);
    HttpRequest request = make_request(body, true);

    ChatMessage result;
    result.role = ChatRole::assistant;

    SseParser parser([&](const SseParser::Event& event) -> bool {
        try {
            json j = json::parse(event.data);
            const auto& candidate = j["candidates"][0];
            const auto& parts = candidate["content"]["parts"];

            for (const auto& part : parts) {
                if (part.contains("text")) {
                    const std::string text = part["text"].get<std::string>();
                    result.content += text;
                    if (on_token && !text.empty()) {
                        if (!on_token(text)) {
                            return false;
                        }
                    }
                } else if (part.contains("functionCall")) {
                    ToolCallRequest call;
                    call.name = part["functionCall"]["name"].get<std::string>();
                    call.arguments = part["functionCall"]["args"].dump();
                    call.id = "gemini_" + call.name;
                    result.tool_calls.push_back(std::move(call));
                }
            }
        } catch (...) {}

        return true;
    });

    const HttpResponse response = transport_->send_streaming(request,
        [&parser](const std::string& chunk) -> bool {
            return parser.feed(chunk);
        });
    parser.finish();

    if (!response.error.empty()) {
        throw std::runtime_error("Gemini streaming request failed: " + response.error);
    }

    return result;
}
