#ifndef PROVIDERS_OPENAI_CLIENT_H
#define PROVIDERS_OPENAI_CLIENT_H

#include <memory>
#include <string>
#include <vector>

#include "../core/interfaces/http_transport.h"
#include "../core/interfaces/model_client.h"
#include "../core/model/chat_message.h"
#include "../core/model/tool_schema.h"

struct OpenAiConfig {
    std::string base_url = "https://api.openai.com";
    std::string api_key;
    std::string model = "gpt-4o";
    int timeout_ms = 300000;
    double temperature = 0.0;
    int max_tokens = 4096;
};

class OpenAiClient : public IModelClient {
public:
    OpenAiClient(OpenAiConfig config, std::shared_ptr<IHttpTransport> transport);

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
    OpenAiConfig config_;
    std::shared_ptr<IHttpTransport> transport_;

    std::string build_request_body(const std::vector<ChatMessage>& messages,
                                   const std::vector<ToolSchema>& tools,
                                   bool stream) const;
    ChatMessage parse_response(const std::string& body) const;
    ChatMessage parse_streaming_response(const std::string& accumulated) const;
    HttpRequest make_request(const std::string& body) const;
};

#endif
