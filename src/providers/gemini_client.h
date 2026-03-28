#ifndef PROVIDERS_GEMINI_CLIENT_H
#define PROVIDERS_GEMINI_CLIENT_H

#include <memory>
#include <string>
#include <vector>

#include "../core/interfaces/http_transport.h"
#include "../core/interfaces/model_client.h"
#include "../core/model/chat_message.h"
#include "../core/model/tool_schema.h"

struct GeminiConfig {
    std::string base_url = "https://generativelanguage.googleapis.com";
    std::string api_key;
    std::string model = "gemini-2.0-flash";
    int timeout_ms = 300000;
    double temperature = 0.0;
    int max_tokens = 4096;
};

class GeminiClient : public IModelClient {
public:
    GeminiClient(GeminiConfig config, std::shared_ptr<IHttpTransport> transport);

    std::string generate(const std::string& prompt) const override;
    const std::string& model() const override;

    ChatMessage chat(const std::vector<ChatMessage>& messages,
                     const std::vector<ToolSchema>& tools) const override;

    ChatMessage chat_streaming(const std::vector<ChatMessage>& messages,
                               const std::vector<ToolSchema>& tools,
                               TokenCallback on_token) const override;

    bool supports_tools() const override { return true; }
    bool supports_streaming() const override { return true; }

private:
    GeminiConfig config_;
    std::shared_ptr<IHttpTransport> transport_;

    std::string build_request_body(const std::vector<ChatMessage>& messages,
                                   const std::vector<ToolSchema>& tools) const;
    ChatMessage parse_response(const std::string& body) const;
    HttpRequest make_request(const std::string& body, bool stream) const;
};

#endif
