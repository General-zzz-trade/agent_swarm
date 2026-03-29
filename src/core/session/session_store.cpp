#include "session_store.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

std::string now_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string truncate(const std::string& s, std::size_t max) {
    if (s.size() <= max) return s;
    return s.substr(0, max) + "...";
}

json message_to_json(const ChatMessage& msg) {
    json j;
    j["role"] = chat_role_to_string(msg.role);
    j["content"] = msg.content;
    if (!msg.name.empty()) j["name"] = msg.name;
    if (!msg.tool_call_id.empty()) j["tool_call_id"] = msg.tool_call_id;
    if (!msg.tool_calls.empty()) {
        json calls = json::array();
        for (const auto& tc : msg.tool_calls) {
            calls.push_back({{"id", tc.id}, {"name", tc.name}, {"arguments", tc.arguments}});
        }
        j["tool_calls"] = calls;
    }
    return j;
}

ChatMessage json_to_message(const json& j) {
    ChatMessage msg;
    const std::string role = j.value("role", "user");
    if (role == "system") msg.role = ChatRole::system;
    else if (role == "assistant") msg.role = ChatRole::assistant;
    else if (role == "tool") msg.role = ChatRole::tool;
    else msg.role = ChatRole::user;

    msg.content = j.value("content", "");
    msg.name = j.value("name", "");
    msg.tool_call_id = j.value("tool_call_id", "");

    if (j.contains("tool_calls")) {
        for (const auto& tc : j["tool_calls"]) {
            ToolCallRequest call;
            call.id = tc.value("id", "");
            call.name = tc.value("name", "");
            call.arguments = tc.value("arguments", "");
            msg.tool_calls.push_back(std::move(call));
        }
    }
    return msg;
}

}  // namespace

SessionStore::SessionStore(std::filesystem::path store_dir)
    : store_dir_(std::move(store_dir)) {
    std::error_code ec;
    std::filesystem::create_directories(store_dir_, ec);
}

std::filesystem::path SessionStore::session_path(const std::string& id) const {
    return store_dir_ / (id + ".json");
}

bool SessionStore::save(const std::string& session_id,
                         const std::vector<ChatMessage>& messages) const {
    try {
        json arr = json::array();
        for (const auto& msg : messages) {
            arr.push_back(message_to_json(msg));
        }

        json doc;
        doc["id"] = session_id;
        doc["modified_at"] = now_timestamp();
        doc["messages"] = arr;

        std::ofstream f(session_path(session_id));
        if (!f) return false;
        f << doc.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<ChatMessage> SessionStore::load(const std::string& session_id) const {
    try {
        std::ifstream f(session_path(session_id));
        if (!f) return {};

        json doc = json::parse(f);
        std::vector<ChatMessage> messages;
        for (const auto& j : doc["messages"]) {
            messages.push_back(json_to_message(j));
        }
        return messages;
    } catch (...) {
        return {};
    }
}

std::vector<SessionStore::SessionInfo> SessionStore::list() const {
    std::vector<SessionInfo> sessions;
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(store_dir_, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;

        try {
            std::ifstream f(entry.path());
            json doc = json::parse(f);

            SessionInfo info;
            info.id = doc.value("id", entry.path().stem().string());
            info.modified_at = doc.value("modified_at", "");
            info.message_count = doc["messages"].size();

            // Find last user message for preview
            for (auto it = doc["messages"].rbegin(); it != doc["messages"].rend(); ++it) {
                if ((*it).value("role", "") == "user") {
                    info.last_message = truncate((*it).value("content", ""), 80);
                    break;
                }
            }

            sessions.push_back(std::move(info));
        } catch (...) {}
    }

    // Sort by modified_at descending
    std::sort(sessions.begin(), sessions.end(),
              [](const SessionInfo& a, const SessionInfo& b) {
                  return a.modified_at > b.modified_at;
              });

    return sessions;
}

bool SessionStore::remove(const std::string& session_id) const {
    std::error_code ec;
    return std::filesystem::remove(session_path(session_id), ec);
}

std::string SessionStore::generate_id() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::ostringstream ss;
    ss << "session-" << std::hex << ms;
    return ss.str();
}
