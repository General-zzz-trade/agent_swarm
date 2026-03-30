#include "memory_store.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

MemoryStore::MemoryStore(std::filesystem::path store_path)
    : store_path_(std::move(store_path)) {}

std::string MemoryStore::now_timestamp() {
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

bool MemoryStore::set(const std::string& key, const std::string& value,
                      const std::string& category) {
    // Update existing entry if key matches
    for (auto& entry : entries_) {
        if (entry.key == key) {
            entry.value = value;
            entry.category = category;
            entry.created_at = now_timestamp();
            return save();
        }
    }
    // Create new entry
    entries_.push_back({key, value, category, now_timestamp()});
    return save();
}

std::string MemoryStore::get(const std::string& key) const {
    for (const auto& entry : entries_) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return "";
}

bool MemoryStore::remove(const std::string& key) {
    auto it = std::remove_if(entries_.begin(), entries_.end(),
        [&key](const MemoryEntry& e) { return e.key == key; });
    if (it == entries_.end()) {
        return false;
    }
    entries_.erase(it, entries_.end());
    save();
    return true;
}

std::vector<MemoryEntry> MemoryStore::list() const {
    return entries_;
}

std::vector<MemoryEntry> MemoryStore::list_by_category(const std::string& category) const {
    std::vector<MemoryEntry> result;
    for (const auto& entry : entries_) {
        if (entry.category == category) {
            result.push_back(entry);
        }
    }
    return result;
}

std::string MemoryStore::format_for_prompt() const {
    if (entries_.empty()) {
        return "";
    }
    std::ostringstream ss;
    for (const auto& entry : entries_) {
        ss << "- " << entry.key << ": " << entry.value << "\n";
    }
    return ss.str();
}

bool MemoryStore::save() const {
    if (store_path_.empty()) {
        return false;
    }
    try {
        // Ensure parent directory exists
        std::error_code ec;
        std::filesystem::create_directories(store_path_.parent_path(), ec);

        json arr = json::array();
        for (const auto& entry : entries_) {
            arr.push_back({
                {"key", entry.key},
                {"value", entry.value},
                {"category", entry.category},
                {"created_at", entry.created_at}
            });
        }

        std::ofstream f(store_path_);
        if (!f) return false;
        f << arr.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool MemoryStore::load() {
    if (store_path_.empty()) {
        return false;
    }
    try {
        std::ifstream f(store_path_);
        if (!f) return false;

        json arr = json::parse(f);
        entries_.clear();
        for (const auto& j : arr) {
            MemoryEntry entry;
            entry.key = j.value("key", "");
            entry.value = j.value("value", "");
            entry.category = j.value("category", "general");
            entry.created_at = j.value("created_at", "");
            if (!entry.key.empty()) {
                entries_.push_back(std::move(entry));
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool MemoryStore::empty() const {
    return entries_.empty();
}

std::size_t MemoryStore::size() const {
    return entries_.size();
}
