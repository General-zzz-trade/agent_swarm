#include "discord_gateway.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "../agent/agent.h"

using json = nlohmann::json;

DiscordGateway::DiscordGateway(std::string bot_token, std::string channel_id,
                               Agent& agent,
                               std::shared_ptr<IHttpTransport> transport,
                               std::filesystem::path workspace_root)
    : bot_token_(std::move(bot_token)),
      channel_id_(std::move(channel_id)),
      agent_(agent),
      transport_(std::move(transport)),
      workspace_root_(std::move(workspace_root)) {}

std::string DiscordGateway::api_call(const std::string& method,
                                     const std::string& path,
                                     const std::string& body) {
    HttpRequest request;
    request.method = method;
    request.url = "https://discord.com/api/v10" + path;
    request.headers.push_back({"Authorization", "Bot " + bot_token_});
    request.headers.push_back({"Content-Type", "application/json"});
    request.headers.push_back({"User-Agent", "Bolt/1.0"});
    request.body = body;
    request.timeout_ms = 10000;

    auto response = transport_->send(request);
    if (response.status_code >= 200 && response.status_code < 300) {
        return response.body;
    }
    return "";
}

void DiscordGateway::send_message(const std::string& channel_id,
                                  const std::string& text) {
    // Split long messages (Discord limit is 2000 chars)
    const size_t MAX_LEN = 1990;

    for (size_t i = 0; i < text.size(); i += MAX_LEN) {
        std::string chunk = text.substr(i, MAX_LEN);
        json body;
        body["content"] = chunk;

        api_call("POST", "/channels/" + channel_id + "/messages", body.dump());
    }
}

void DiscordGateway::send_typing(const std::string& channel_id) {
    api_call("POST", "/channels/" + channel_id + "/typing");
}

void DiscordGateway::handle_message(const std::string& channel_id,
                                    const std::string& text) {
    // Handle commands
    if (text == "!start" || text == "!help") {
        send_message(channel_id,
            "**Bolt -- AI Coding Agent**\n\n"
            "Send me any coding task and I'll help!\n\n"
            "Commands:\n"
            "`!clear` -- Clear conversation history\n"
            "`!model` -- Show current model\n"
            "`!status` -- Show agent status\n"
            "`!help` -- Show this help");
        return;
    }

    if (text == "!clear") {
        agent_.clear_history();
        send_message(channel_id, "History cleared.");
        return;
    }

    if (text == "!model") {
        send_message(channel_id, "Model: `" + agent_.model() + "`");
        return;
    }

    if (text == "!status") {
        auto usage = agent_.last_token_usage();
        std::string status = "**Status**\n"
            "Model: `" + agent_.model() + "`\n"
            "Tokens: " + std::to_string(usage.input_tokens) + " in / " +
            std::to_string(usage.output_tokens) + " out";
        send_message(channel_id, status);
        return;
    }

    // Send typing indicator
    send_typing(channel_id);

    // Run agent turn
    try {
        std::string reply = agent_.run_turn(text);

        if (reply.empty()) {
            send_message(channel_id, "(No response)");
        } else {
            send_message(channel_id, reply);
        }
    } catch (const std::exception& e) {
        send_message(channel_id, "Error: " + std::string(e.what()));
    }
}

void DiscordGateway::poll_messages() {
    std::string path = "/channels/" + channel_id_ + "/messages?limit=10";
    if (!last_message_id_.empty()) {
        path += "&after=" + last_message_id_;
    }

    std::string response = api_call("GET", path);
    if (response.empty()) return;

    try {
        auto messages = json::parse(response);
        if (!messages.is_array()) return;

        // Messages come newest-first from Discord, process oldest-first
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            const auto& msg = *it;
            std::string msg_id = msg.value("id", "");
            std::string content = msg.value("content", "");
            std::string author_id;
            if (msg.contains("author")) {
                author_id = msg["author"].value("id", "");
            }

            if (!msg_id.empty() && msg_id > last_message_id_) {
                last_message_id_ = msg_id;
            }

            // Skip own messages
            if (author_id == bot_user_id_) continue;

            if (!content.empty()) {
                handle_message(channel_id_, content);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Discord parse error: " << e.what() << std::endl;
    }
}

int DiscordGateway::run() {
    // Verify bot token by calling /users/@me
    std::string me = api_call("GET", "/users/@me");
    if (me.empty()) {
        std::cerr << "Error: Invalid Discord bot token or network error.\n";
        std::cerr << "Set DISCORD_BOT_TOKEN environment variable.\n";
        return 1;
    }

    try {
        auto j = json::parse(me);
        std::string bot_name = j.value("username", "unknown");
        bot_user_id_ = j.value("id", "");
        std::cout << "Bolt Discord Gateway\n";
        std::cout << "  Bot: " << bot_name << "\n";
        std::cout << "  Channel: " << channel_id_ << "\n";
        std::cout << "  Model: " << agent_.model() << "\n";
        std::cout << "  Workspace: " << workspace_root_.string() << "\n";
        std::cout << "  Listening for messages... (Ctrl+C to stop)\n\n";
    } catch (...) {}

    // Polling loop
    while (running_) {
        try {
            poll_messages();
        } catch (const std::exception& e) {
            std::cerr << "Poll error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return 0;
}

void DiscordGateway::stop() {
    running_ = false;
}
