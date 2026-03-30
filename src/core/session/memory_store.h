#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct MemoryEntry {
    std::string key;        // e.g. "user.language", "project.build_system"
    std::string value;      // e.g. "Chinese", "CMake"
    std::string category;   // "user", "project", "preference"
    std::string created_at;
};

class MemoryStore {
public:
    MemoryStore() = default;
    explicit MemoryStore(std::filesystem::path store_path);

    // CRUD
    bool set(const std::string& key, const std::string& value,
             const std::string& category = "general");
    std::string get(const std::string& key) const;
    bool remove(const std::string& key);
    std::vector<MemoryEntry> list() const;
    std::vector<MemoryEntry> list_by_category(const std::string& category) const;

    // For system prompt injection
    std::string format_for_prompt() const;

    // Persistence
    bool save() const;
    bool load();

    bool empty() const;
    std::size_t size() const;

private:
    std::filesystem::path store_path_;
    std::vector<MemoryEntry> entries_;

    static std::string now_timestamp();
};
