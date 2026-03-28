#include "ollama_chat_client.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace {

json tool_schema_to_ollama(const ToolSchema& schema) {
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

    return {
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
}

json message_to_ollama(const ChatMessage& msg) {
    json j;
    j["role"] = chat_role_to_string(msg.role);
    j["content"] = msg.content;

    if (msg.role == ChatRole::assistant && msg.has_tool_calls()) {
        json calls = json::array();
        for (const auto& tc : msg.tool_calls) {
            json args;
            try {
                args = json::parse(tc.arguments);
            } catch (...) {
                args = {{"args", tc.arguments}};
            }
            calls.push_back({
                {"function", {
                    {"name", tc.name},
                    {"arguments", args}
                }}
            });
        }
        j["tool_calls"] = calls;
    }

    return j;
}

}  // namespace

OllamaChatClient::OllamaChatClient(std::string model,
                                   OllamaConnectionConfig config,
                                   std::shared_ptr<IHttpTransport> transport)
    : model_(std::move(model)),
      config_(std::move(config)),
      transport_(std::move(transport)) {
    if (transport_ == nullptr) {
        throw std::invalid_argument("OllamaChatClient requires an HTTP transport");
    }
}

std::string OllamaChatClient::build_url() const {
    std::ostringstream url;
    url << "http://" << config_.host << ":" << config_.port << "/api/chat";
    return url.str();
}

std::string OllamaChatClient::generate(const std::string& prompt) const {
    std::vector<ChatMessage> messages;
    messages.push_back({ChatRole::user, prompt});
    ChatMessage response = chat(messages, {});
    return response.content;
}

const std::string& OllamaChatClient::model() const {
    return model_;
}

std::string OllamaChatClient::build_request_body(const std::vector<ChatMessage>& messages,
                                                  const std::vector<ToolSchema>& tools,
                                                  bool stream) const {
    json body;
    body["model"] = model_;
    body["stream"] = stream;

    json msg_array = json::array();
    for (const auto& msg : messages) {
        msg_array.push_back(message_to_ollama(msg));
    }
    body["messages"] = msg_array;

    if (!tools.empty()) {
        json tools_array = json::array();
        for (const auto& tool : tools) {
            tools_array.push_back(tool_schema_to_ollama(tool));
        }
        body["tools"] = tools_array;
    }

    return body.dump();
}

ChatMessage OllamaChatClient::chat(const std::vector<ChatMessage>& messages,
                                    const std::vector<ToolSchema>& tools) const {
    const std::string body = build_request_body(messages, tools, false);

    HttpRequest request;
    request.method = "POST";
    request.url = build_url();
    request.body = body;
    request.timeout_ms = config_.receive_timeout_ms;

    const HttpResponse response = transport_->send(request);

    if (!response.error.empty()) {
        throw std::runtime_error("Ollama chat request failed: " + response.error);
    }
    if (response.status_code != 200) {
        throw std::runtime_error("Ollama chat returned HTTP " +
                                 std::to_string(response.status_code) + ": " + response.body);
    }

    json j = json::parse(response.body);

    ChatMessage result;
    result.role = ChatRole::assistant;

    const auto& msg = j["message"];
    if (msg.contains("content")) {
        result.content = msg["content"].get<std::string>();
    }

    if (msg.contains("tool_calls")) {
        for (const auto& tc : msg["tool_calls"]) {
            ToolCallRequest call;
            call.name = tc["function"]["name"].get<std::string>();
            call.arguments = tc["function"]["arguments"].dump();
            call.id = "ollama_" + call.name;
            result.tool_calls.push_back(std::move(call));
        }
    }

    return result;
}

ChatMessage OllamaChatClient::chat_streaming(const std::vector<ChatMessage>& messages,
                                              const std::vector<ToolSchema>& tools,
                                              TokenCallback on_token) const {
    const std::string body = build_request_body(messages, tools, true);

    HttpRequest request;
    request.method = "POST";
    request.url = build_url();
    request.body = body;
    request.timeout_ms = config_.receive_timeout_ms;

    ChatMessage result;
    result.role = ChatRole::assistant;

    // Ollama streaming uses line-delimited JSON (not SSE)
    std::string line_buffer;

    const HttpResponse response = transport_->send_streaming(request,
        [&](const std::string& chunk) -> bool {
            line_buffer += chunk;

            std::string::size_type pos = 0;
            while (pos < line_buffer.size()) {
                const auto nl = line_buffer.find('\n', pos);
                if (nl == std::string::npos) {
                    break;
                }

                std::string line = line_buffer.substr(pos, nl - pos);
                pos = nl + 1;

                if (line.empty() || line == "\r") {
                    continue;
                }

                try {
                    json j = json::parse(line);
                    if (j.contains("message")) {
                        const auto& msg = j["message"];
                        if (msg.contains("content")) {
                            const std::string token = msg["content"].get<std::string>();
                            result.content += token;
                            if (on_token && !token.empty()) {
                                if (!on_token(token)) {
                                    return false;
                                }
                            }
                        }
                        if (msg.contains("tool_calls")) {
                            for (const auto& tc : msg["tool_calls"]) {
                                ToolCallRequest call;
                                call.name = tc["function"]["name"].get<std::string>();
                                call.arguments = tc["function"]["arguments"].dump();
                                call.id = "ollama_" + call.name;
                                result.tool_calls.push_back(std::move(call));
                            }
                        }
                    }

                    if (j.value("done", false)) {
                        return false;
                    }
                } catch (...) {}
            }

            line_buffer.erase(0, pos);
            return true;
        });

    if (!response.error.empty()) {
        throw std::runtime_error("Ollama streaming request failed: " + response.error);
    }

    return result;
}
