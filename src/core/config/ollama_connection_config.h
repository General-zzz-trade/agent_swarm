#ifndef CORE_CONFIG_OLLAMA_CONNECTION_CONFIG_H
#define CORE_CONFIG_OLLAMA_CONNECTION_CONFIG_H

#include <string>

struct OllamaConnectionConfig {
    std::string host = "127.0.0.1";
    unsigned short port = 11434;
    std::string generate_path = "/api/generate";
    int resolve_timeout_ms = 5000;
    int connect_timeout_ms = 5000;
    int send_timeout_ms = 15000;
    int receive_timeout_ms = 300000;
};

#endif
