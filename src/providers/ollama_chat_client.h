#ifndef PROVIDERS_OLLAMA_CHAT_CLIENT_H
#define PROVIDERS_OLLAMA_CHAT_CLIENT_H

#include <memory>
#include <string>
#include <vector>

#include "../core/config/ollama_connection_config.h"
#include "../core/interfaces/http_transport.h"
#include "../core/interfaces/model_client.h"
#include "../core/model/chat_message.h"
#include "../core/model/tool_schema.h"

class OllamaChatClient : public IModelClient {
public:
    OllamaChatClient(std::string model,
                     OllamaConnectionConfig config,
                     std::shared_ptr<IHttpTransport> transport);

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
    std::string model_;
    OllamaConnectionConfig config_;
    std::shared_ptr<IHttpTransport> transport_;

    std::string build_url() const;
    std::string build_request_body(const std::vector<ChatMessage>& messages,
                                   const std::vector<ToolSchema>& tools,
                                   bool stream) const;
};

#endif
