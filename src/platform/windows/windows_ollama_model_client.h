#ifndef PLATFORM_WINDOWS_WINDOWS_OLLAMA_MODEL_CLIENT_H
#define PLATFORM_WINDOWS_WINDOWS_OLLAMA_MODEL_CLIENT_H

#include <string>

#include "../../core/config/ollama_connection_config.h"
#include "../../core/interfaces/model_client.h"

class WindowsOllamaModelClient : public IModelClient {
public:
    explicit WindowsOllamaModelClient(std::string model,
                                      OllamaConnectionConfig config = {});

    std::string generate(const std::string& prompt) const override;
    const std::string& model() const override;

private:
    std::string model_;
    OllamaConnectionConfig config_;
};

#endif
