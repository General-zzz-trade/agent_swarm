#include "setup_wizard.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

namespace {

// --- ANSI color helpers ---
const std::string kReset = "\033[0m";
const std::string kBold = "\033[1m";
const std::string kDim = "\033[2m";
const std::string kCyan = "\033[36m";
const std::string kGreen = "\033[32m";
const std::string kYellow = "\033[33m";
const std::string kMagenta = "\033[35m";
const std::string kWhite = "\033[37m";
const std::string kBoldCyan = "\033[1;36m";
const std::string kBoldGreen = "\033[1;32m";
const std::string kBoldYellow = "\033[1;33m";
const std::string kBoldWhite = "\033[1;37m";

struct ModelOption {
    std::string id;
    std::string description;
};

struct ProviderInfo {
    std::string id;
    std::string display_name;
    std::string description;
    std::string env_var;  // empty for ollama
    std::vector<ModelOption> models;
};

std::vector<ProviderInfo> get_providers() {
    return {
        {"ollama", "Ollama", "Local models, no API key", "", {
            {"qwen3:8b", "Qwen 3 8B — good balance"},
            {"llama3:8b", "Meta Llama 3 8B"},
            {"deepseek-r1:8b", "DeepSeek R1 reasoning"},
            {"codellama:13b", "Code-focused 13B"},
        }},
        {"openai", "OpenAI", "GPT-4o, o3, o4-mini", "OPENAI_API_KEY", {
            {"gpt-4o", "GPT-4o — best balance ($2.5/$10/M)"},
            {"gpt-4o-mini", "GPT-4o Mini — cheap ($0.15/$0.6/M)"},
            {"o3-mini", "o3-mini — reasoning ($1.1/$4.4/M)"},
            {"o4-mini", "o4-mini — latest reasoning"},
            {"gpt-4.1", "GPT-4.1 — code-focused"},
        }},
        {"claude", "Claude (Anthropic)", "Sonnet 4.6, Opus 4.6, Haiku 4.5", "ANTHROPIC_API_KEY", {
            {"claude-sonnet-4-6-20250217", "Sonnet 4.6 — best balance ($3/$15/M)"},
            {"claude-opus-4-6-20250205", "Opus 4.6 — most capable ($15/$75/M)"},
            {"claude-haiku-4-5-20251001", "Haiku 4.5 — fastest ($1/$5/M)"},
        }},
        {"gemini", "Gemini (Google)", "Flash, Pro", "GEMINI_API_KEY", {
            {"gemini-2.0-flash", "Flash 2.0 — fast, cheap"},
            {"gemini-2.0-pro", "Pro 2.0 — most capable"},
            {"gemini-2.5-pro-preview", "Pro 2.5 Preview — latest"},
        }},
        {"groq", "Groq", "Ultra-fast inference", "GROQ_API_KEY", {
            {"llama-3.3-70b-versatile", "Llama 3.3 70B — best quality"},
            {"llama-3.1-8b-instant", "Llama 3.1 8B — fastest"},
            {"mixtral-8x7b-32768", "Mixtral 8x7B — 32K context"},
        }},
        {"deepseek", "DeepSeek (深度求索)", "V3.2, R1 — 超低价格", "DEEPSEEK_API_KEY", {
            {"deepseek-chat", "DeepSeek-V3.2 — 通用 ($0.27/$1.1/M)"},
            {"deepseek-reasoner", "DeepSeek-R1 — 推理 ($0.55/$2.19/M)"},
        }},
        {"qwen", "通义千问 (Qwen)", "阿里云百炼 — Qwen3.5", "DASHSCOPE_API_KEY", {
            {"qwen-plus", "Qwen-Plus — 性价比之选"},
            {"qwen-max", "Qwen-Max — 最强能力"},
            {"qwen-turbo", "Qwen-Turbo — 极速低价"},
            {"qwen3.5-plus", "Qwen3.5-Plus — 最新 1M 上下文"},
        }},
        {"zhipu", "智谱 GLM", "GLM-4 / GLM-5 系列", "ZHIPU_API_KEY", {
            {"glm-4-flash", "GLM-4-Flash — 免费额度"},
            {"glm-4-plus", "GLM-4-Plus — 强能力"},
            {"glm-4-long", "GLM-4-Long — 200K 上下文"},
            {"glm-4-airx", "GLM-4-AirX — 极速推理"},
        }},
        {"moonshot", "月之暗面 (Kimi)", "Kimi K2.5 / moonshot-v1", "MOONSHOT_API_KEY", {
            {"moonshot-v1-128k", "moonshot-v1-128k — 推荐，128K上下文"},
            {"moonshot-v1-32k", "moonshot-v1-32k — 32K上下文"},
            {"moonshot-v1-8k", "moonshot-v1-8k — 快速，8K上下文"},
        }},
        {"baichuan", "百川智能", "Baichuan4", "BAICHUAN_API_KEY", {
            {"Baichuan4", "Baichuan4 — 最新最强"},
            {"Baichuan3-Turbo", "Baichuan3-Turbo — 快速"},
        }},
        {"doubao", "豆包 (字节跳动)", "火山引擎方舟", "VOLC_API_KEY", {
            {"doubao-pro-32k", "Pro, 32K context"},
            {"doubao-lite-32k", "Lite, very cheap"},
        }},
    };
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string read_line_stdin() {
    std::string line;
    if (!std::getline(std::cin, line)) {
        return "";
    }
    return trim(line);
}

int read_choice(int min_val, int max_val) {
    while (true) {
        std::string input = read_line_stdin();
        if (input.empty()) continue;
        try {
            int choice = std::stoi(input);
            if (choice >= min_val && choice <= max_val) {
                return choice;
            }
        } catch (...) {}
        std::cout << kDim << " Invalid choice. Enter " << min_val << "-"
                  << max_val << ": " << kReset << std::flush;
    }
}

std::string format_size(int64_t bytes) {
    if (bytes <= 0) return "";
    double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    std::ostringstream oss;
    oss.precision(1);
    oss << std::fixed << gb << " GB";
    return oss.str();
}

// --- Lightweight HTTP GET to localhost (for Ollama detection) ---
std::string http_get_localhost(int port, const std::string& path, int timeout_ms = 2000) {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // Set timeout for connect
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        ::close(sock);
#endif
        return "";
    }

    std::string request = "GET " + path + " HTTP/1.0\r\nHost: localhost\r\n\r\n";
    if (::send(sock, request.c_str(), request.size(), 0) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        ::close(sock);
#endif
        return "";
    }

    std::string response;
    char buf[4096];
    while (true) {
        auto n = ::recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        response += buf;
    }

#ifdef _WIN32
    closesocket(sock);
#else
    ::close(sock);
#endif

    // Extract body after \r\n\r\n
    auto body_pos = response.find("\r\n\r\n");
    if (body_pos == std::string::npos) return "";
    return response.substr(body_pos + 4);
}

struct OllamaModel {
    std::string name;
    int64_t size = 0;
};

std::vector<OllamaModel> detect_ollama_models() {
    std::vector<OllamaModel> models;
    try {
        std::string body = http_get_localhost(11434, "/api/tags");
        if (body.empty()) return models;

        auto json = nlohmann::json::parse(body);
        if (!json.contains("models") || !json["models"].is_array()) return models;

        for (const auto& m : json["models"]) {
            OllamaModel om;
            if (m.contains("name") && m["name"].is_string()) {
                om.name = m["name"].get<std::string>();
            }
            if (m.contains("size") && m["size"].is_number()) {
                om.size = m["size"].get<int64_t>();
            }
            if (!om.name.empty()) {
                models.push_back(om);
            }
        }
    } catch (...) {
        // Ollama not running or parse error
    }
    return models;
}

void print_header() {
    std::cout << "\n";
    std::cout << " " << kBoldCyan << "⚡ Bolt — First-Time Setup" << kReset << "\n";
    std::cout << "\n";
}

void print_provider_menu(const std::vector<ProviderInfo>& providers,
                         const std::string& current_provider = "") {
    std::cout << " " << kBoldWhite << "Select your LLM provider:" << kReset << "\n\n";

    for (size_t i = 0; i < providers.size(); ++i) {
        const auto& p = providers[i];
        bool is_current = (p.id == current_provider);
        std::string marker = is_current ? " *" : "  ";
        std::string num_color = is_current ? kBoldGreen : kBoldCyan;

        std::cout << marker << " " << num_color << (i + 1) << ")" << kReset
                  << " " << kBold << p.display_name << kReset
                  << "  " << kDim << p.description << kReset << "\n";
    }
    std::cout << "\n";
}

std::string select_model_for_provider(const ProviderInfo& provider,
                                      const std::string& current_model = "") {
    // Special handling for Ollama: try to detect local models
    if (provider.id == "ollama") {
        auto local_models = detect_ollama_models();
        if (!local_models.empty()) {
            std::cout << " " << kBoldWhite << "Available local models:" << kReset << "\n\n";
            for (size_t i = 0; i < local_models.size(); ++i) {
                const auto& m = local_models[i];
                bool is_current = (m.name == current_model);
                std::string marker = is_current ? " *" : "  ";
                std::string num_color = is_current ? kBoldGreen : kBoldCyan;
                std::string size_str = format_size(m.size);

                std::cout << marker << " " << num_color << (i + 1) << ")" << kReset
                          << " " << kBold << m.name << kReset;
                if (!size_str.empty()) {
                    std::cout << "  " << kDim << size_str << kReset;
                }
                std::cout << "\n";
            }
            std::cout << "\n";
            std::cout << " " << kBoldWhite << "Choose model [1-" << local_models.size()
                      << "] or type name: " << kReset << std::flush;

            std::string input = read_line_stdin();
            if (input.empty() && !current_model.empty()) return current_model;

            try {
                int idx = std::stoi(input);
                if (idx >= 1 && idx <= static_cast<int>(local_models.size())) {
                    return local_models[idx - 1].name;
                }
            } catch (...) {}

            // Treat as model name
            if (!input.empty()) return input;
            return local_models[0].name;
        } else {
            std::cout << " " << kYellow << "Ollama not detected." << kReset
                      << " Install from https://ollama.ai then:\n"
                      << "   " << kDim << "ollama pull qwen3:8b" << kReset << "\n\n";
            std::cout << " " << kBoldWhite << "Enter model name [qwen3:8b]: " << kReset
                      << std::flush;
            std::string input = read_line_stdin();
            if (input.empty()) return "qwen3:8b";
            return input;
        }
    }

    // For cloud providers: check API key
    if (!provider.env_var.empty()) {
        const char* key = std::getenv(provider.env_var.c_str());
        if (key == nullptr || std::string(key).empty()) {
            std::cout << " " << kYellow << "Warning:" << kReset
                      << " Set " << kBold << provider.env_var << kReset
                      << " environment variable first.\n\n";
        }
    }

    // Show model list
    const auto& models = provider.models;
    std::cout << " " << kBoldWhite << "Select model:" << kReset << "\n\n";

    for (size_t i = 0; i < models.size(); ++i) {
        const auto& m = models[i];
        bool is_current = (m.id == current_model);
        std::string marker = is_current ? " *" : "  ";
        std::string num_color = is_current ? kBoldGreen : kBoldCyan;

        std::cout << marker << " " << num_color << (i + 1) << ")" << kReset
                  << " " << kBold << m.id << kReset
                  << "  " << kDim << m.description << kReset << "\n";
    }
    std::cout << "\n";
    std::cout << " " << kBoldWhite << "Choose [1-" << models.size()
              << "] or type name: " << kReset << std::flush;

    std::string input = read_line_stdin();
    if (input.empty()) {
        if (!current_model.empty()) return current_model;
        return models[0].id;
    }

    try {
        int idx = std::stoi(input);
        if (idx >= 1 && idx <= static_cast<int>(models.size())) {
            return models[idx - 1].id;
        }
    } catch (...) {}

    // Treat as model name
    return input;
}

void print_confirmation(const std::string& provider, const std::string& model,
                        const std::filesystem::path& config_path) {
    std::cout << "\n";
    std::cout << " " << kBoldGreen << "✓ Configuration saved!" << kReset << "\n";
    std::cout << "   " << kBold << "Provider:" << kReset << " " << provider << "\n";
    std::cout << "   " << kBold << "Model:   " << kReset << " " << model << "\n";
    std::cout << "   " << kBold << "Config:  " << kReset << " " << config_path.string() << "\n";
    std::cout << "\n";
    std::cout << "   " << kDim << "To change later: bolt agent --model <name> or use /model in interactive mode"
              << kReset << "\n\n";
}

SetupResult run_wizard_flow(const std::string& current_provider,
                            const std::string& current_model,
                            bool show_header) {
    auto providers = get_providers();

    if (show_header) {
        print_header();
    }

    print_provider_menu(providers, current_provider);

    std::cout << " " << kBoldWhite << "Choose [1-" << providers.size() << "]: "
              << kReset << std::flush;

    int provider_choice = read_choice(1, static_cast<int>(providers.size()));
    const auto& selected = providers[provider_choice - 1];

    std::cout << "\n";

    std::string model = select_model_for_provider(selected, current_model);

    // Prompt for API key if needed and not already set
    if (!selected.env_var.empty()) {
        const char* existing = std::getenv(selected.env_var.c_str());
        bool has_key = existing && std::string(existing).size() > 5;

        if (!has_key) {
            std::cout << "\n";
            std::cout << " " << kBoldWhite << "API Key Setup" << kReset << "\n\n";
            std::cout << "   Paste your " << kBold << selected.env_var << kReset << " below.\n";
            std::cout << "   " << kDim << "(Get one at the provider's website, then paste here)" << kReset << "\n\n";
            std::cout << " " << kBoldWhite << selected.env_var << "=  " << kReset << std::flush;

            std::string api_key = read_line_stdin();

            if (!api_key.empty() && api_key.size() > 5) {
                // Write to ~/.bolt/env file for persistence
                const char* home = std::getenv("HOME");
#ifdef _WIN32
                if (!home) home = std::getenv("USERPROFILE");
#endif
                if (home) {
                    auto env_path = std::filesystem::path(home) / ".bolt" / "env";
                    std::filesystem::create_directories(env_path.parent_path());

                    // Read existing env file
                    std::string existing_content;
                    if (std::filesystem::exists(env_path)) {
                        std::ifstream ef(env_path);
                        existing_content.assign(std::istreambuf_iterator<char>(ef), {});
                    }

                    // Remove old entry for this key if exists
                    std::ostringstream new_content;
                    std::istringstream old_stream(existing_content);
                    std::string line;
                    while (std::getline(old_stream, line)) {
                        if (line.find(selected.env_var + "=") != 0) {
                            new_content << line << "\n";
                        }
                    }
                    new_content << selected.env_var << "=" << api_key << "\n";

                    std::ofstream of(env_path);
                    of << new_content.str();
                    of.close();

                    // Restrict file permissions (owner-only read/write)
#ifndef _WIN32
                    chmod(env_path.c_str(), 0600);
#endif

                    // Also set in current process environment
#ifdef _WIN32
                    _putenv_s(selected.env_var.c_str(), api_key.c_str());
#else
                    setenv(selected.env_var.c_str(), api_key.c_str(), 1);
#endif

                    std::cout << " " << kBoldGreen << "✓" << kReset
                              << " Key saved to ~/.bolt/env\n";
                    std::cout << "   " << kDim << "To use in new terminals: source ~/.bolt/env" << kReset << "\n";
                }
            } else {
                std::cout << " " << kDim << "Skipped. Set it later:" << kReset << "\n";
                std::cout << "   " << kBold << "export " << selected.env_var << "=your-key" << kReset << "\n";
            }
        } else {
            std::cout << "\n " << kBoldGreen << "✓" << kReset
                      << " " << selected.env_var << " already set\n";
        }
    }

    SetupResult result;
    result.provider = selected.id;
    result.model = model;
    result.completed = true;
    return result;
}

void apply_result_to_config(AppConfig& config, const std::string& provider,
                            const std::string& model) {
    config.provider = provider;
    config.default_model = model;

    if (provider == "openai") {
        config.openai_model = model;
    } else if (provider == "claude") {
        config.claude_model = model;
    } else if (provider == "gemini") {
        config.gemini_model = model;
    } else if (provider == "groq") {
        config.groq_model = model;
    } else if (provider == "deepseek") {
        config.deepseek_model = model;
    } else if (provider == "qwen") {
        config.qwen_model = model;
    } else if (provider == "zhipu") {
        config.zhipu_model = model;
    } else if (provider == "moonshot") {
        config.moonshot_model = model;
    } else if (provider == "baichuan") {
        config.baichuan_model = model;
    } else if (provider == "doubao") {
        config.doubao_model = model;
    }
}

}  // namespace

std::filesystem::path get_global_config_path() {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home) home = std::getenv("USERPROFILE");
#endif
    if (!home) return {};
    return std::filesystem::path(home) / ".bolt" / "config.json";
}

bool is_setup_complete() {
    auto path = get_global_config_path();
    if (path.empty()) return false;
    if (!std::filesystem::exists(path)) return false;

    try {
        std::ifstream f(path);
        if (!f) return false;
        auto json = nlohmann::json::parse(f);
        return json.contains("provider") && json["provider"].is_string() &&
               !json["provider"].get<std::string>().empty();
    } catch (...) {
        return false;
    }
}

bool save_setup_config(const SetupResult& result) {
    auto path = get_global_config_path();
    if (path.empty()) return false;

    try {
        std::filesystem::create_directories(path.parent_path());

        nlohmann::json json;
        json["provider"] = result.provider;
        json["model"] = result.model;

        std::ofstream f(path);
        if (!f) return false;
        f << json.dump(2) << "\n";
        return f.good();
    } catch (...) {
        return false;
    }
}

bool load_setup_config(AppConfig& config) {
    auto path = get_global_config_path();
    if (path.empty()) return false;
    if (!std::filesystem::exists(path)) return false;

    try {
        std::ifstream f(path);
        if (!f) return false;
        auto json = nlohmann::json::parse(f);

        if (!json.contains("provider") || !json["provider"].is_string()) return false;

        std::string provider = json["provider"].get<std::string>();
        std::string model;
        if (json.contains("model") && json["model"].is_string()) {
            model = json["model"].get<std::string>();
        }

        if (provider.empty()) return false;

        apply_result_to_config(config, provider, model);
        return true;
    } catch (...) {
        return false;
    }
}

SetupResult run_setup_wizard() {
    auto result = run_wizard_flow("", "", true);

    if (result.completed) {
        save_setup_config(result);
        print_confirmation(result.provider, result.model, get_global_config_path());
    }

    return result;
}

SetupResult run_model_selector(const std::string& current_provider,
                               const std::string& current_model) {
    std::cout << "\n";
    std::cout << " " << kBoldCyan << "⚡ Bolt — Model Selection" << kReset << "\n";
    std::cout << "   " << kDim << "Current: " << current_provider << " / "
              << current_model << kReset << "\n\n";

    auto result = run_wizard_flow(current_provider, current_model, false);

    if (result.completed) {
        save_setup_config(result);
        print_confirmation(result.provider, result.model, get_global_config_path());
    }

    return result;
}
