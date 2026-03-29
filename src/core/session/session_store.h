#ifndef CORE_SESSION_SESSION_STORE_H
#define CORE_SESSION_SESSION_STORE_H

#include <filesystem>
#include <string>
#include <vector>

#include "../model/chat_message.h"

/// Persists conversation history to disk so sessions can be resumed.
///
/// Storage: .bolt/sessions/<session_id>.json
/// Format: JSON array of {role, content, name, tool_call_id, tool_calls}
///
/// Usage:
///   SessionStore store(workspace / ".bolt" / "sessions");
///   store.save("session-1", messages);
///   auto messages = store.load("session-1");
///   auto sessions = store.list();  // returns all session IDs
class SessionStore {
public:
    struct SessionInfo {
        std::string id;
        std::string last_message;      // First 80 chars of last user message
        std::size_t message_count;
        std::string modified_at;       // ISO timestamp
    };

    explicit SessionStore(std::filesystem::path store_dir);

    /// Save conversation messages to a session file.
    bool save(const std::string& session_id,
              const std::vector<ChatMessage>& messages) const;

    /// Load messages from a session file.
    std::vector<ChatMessage> load(const std::string& session_id) const;

    /// List all saved sessions, most recent first.
    std::vector<SessionInfo> list() const;

    /// Delete a session file.
    bool remove(const std::string& session_id) const;

    /// Generate a new unique session ID.
    static std::string generate_id();

private:
    std::filesystem::path store_dir_;
    std::filesystem::path session_path(const std::string& id) const;
};

#endif
