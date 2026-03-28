#include "windows_ollama_model_client.h"
#include "ollama_json_utils.h"
#include "winhttp_transport.h"

#include <sstream>
#include <stdexcept>

WindowsOllamaModelClient::WindowsOllamaModelClient(std::string model,
                                                   OllamaConnectionConfig config,
                                                   std::shared_ptr<IHttpTransport> transport)
    : model_(std::move(model)),
      config_(std::move(config)),
      transport_(std::move(transport)) {
    if (transport_ == nullptr) {
        transport_ = std::make_shared<WinHttpTransport>();
    }
}

std::string WindowsOllamaModelClient::build_url() const {
    std::ostringstream url;
    url << "http://" << config_.host << ":" << config_.port << config_.generate_path;
    return url.str();
}

std::string WindowsOllamaModelClient::generate(const std::string& prompt) const {
    const std::string request_body =
        "{\"model\":\"" + ollama_json::escape_json_string(model_) +
        "\",\"prompt\":\"" + ollama_json::escape_json_string(prompt) +
        "\",\"stream\":false}";

    HttpRequest request;
    request.method = "POST";
    request.url = build_url();
    request.body = request_body;
    request.timeout_ms = config_.receive_timeout_ms;

    const HttpResponse response = transport_->send(request);

    if (!response.error.empty()) {
        throw std::runtime_error("Ollama request failed: " + response.error);
    }

    if (response.status_code != 200) {
        std::string error_message;
        try {
            error_message = ollama_json::extract_top_level_json_string_field(
                response.body, "error");
        } catch (...) {}

        std::ostringstream output;
        output << "Ollama request failed with HTTP " << response.status_code;
        if (!error_message.empty()) {
            output << ": " << error_message;
        } else if (!response.body.empty()) {
            output << ": " << response.body;
        }
        throw std::runtime_error(output.str());
    }

    const std::string error =
        ollama_json::extract_top_level_json_string_field(response.body, "error");
    if (!error.empty()) {
        throw std::runtime_error(error);
    }

    const std::string result =
        ollama_json::extract_top_level_json_string_field(response.body, "response");
    if (result.empty()) {
        throw std::runtime_error("Missing response field from Ollama");
    }
    return result;
}

const std::string& WindowsOllamaModelClient::model() const {
    return model_;
}
