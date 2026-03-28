#ifndef PLATFORM_WINDOWS_WINDOWS_OLLAMA_MODEL_CLIENT_H
#define PLATFORM_WINDOWS_WINDOWS_OLLAMA_MODEL_CLIENT_H

#include <memory>
#include <string>

#include "../../core/config/ollama_connection_config.h"
#include "../../core/interfaces/http_transport.h"
#include "../../core/interfaces/model_client.h"

class WindowsOllamaModelClient : public IModelClient {
public:
    explicit WindowsOllamaModelClient(std::string model,
                                      OllamaConnectionConfig config = {},
                                      std::shared_ptr<IHttpTransport> transport = nullptr);

    std::string generate(const std::string& prompt) const override;
    const std::string& model() const override;

private:
    std::string model_;
    OllamaConnectionConfig config_;
    std::shared_ptr<IHttpTransport> transport_;

    std::string build_url() const;
};

#endif
