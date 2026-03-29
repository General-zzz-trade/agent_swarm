#include "prompt_compressor.h"

#include <algorithm>
#include <sstream>

PromptCompressor::PromptCompressor() : config_() {}

PromptCompressor::PromptCompressor(Config config)
    : config_(std::move(config)) {}

std::vector<ChatMessage> PromptCompressor::compress(
    const std::vector<ChatMessage>& messages) const {

    // Step 1: Trim to max history window (preserves all recent context)
    std::vector<ChatMessage> result = trim_history(messages);

    // Step 2: Lossless truncation only if configured (default: off)
    if (config_.max_tool_result_chars > 0) {
        for (auto& msg : result) {
            if (msg.role == ChatRole::tool) {
                msg.content = truncate_tool_result(msg.content);
            }
        }
    }

    // Step 3: Deduplicate whitespace (lossless — removes only redundant blank lines)
    if (config_.deduplicate_whitespace) {
        for (auto& msg : result) {
            std::string& content = msg.content;
            // Replace 3+ consecutive newlines with 2
            std::string cleaned;
            cleaned.reserve(content.size());
            int consecutive_newlines = 0;
            for (char ch : content) {
                if (ch == '\n') {
                    ++consecutive_newlines;
                    if (consecutive_newlines <= 2) {
                        cleaned += ch;
                    }
                } else {
                    consecutive_newlines = 0;
                    cleaned += ch;
                }
            }
            content = std::move(cleaned);
        }
    }

    // Step 4: Hard cap only if configured (default: off for lossless)
    if (config_.max_total_chars > 0) {
        std::size_t total_chars = 0;
        for (const auto& msg : result) {
            total_chars += msg.content.size();
        }

        while (total_chars > config_.max_total_chars && result.size() > 2) {
            auto it = result.begin();
            while (it != result.end() && it->role == ChatRole::system) ++it;
            if (it == result.end()) break;
            total_chars -= it->content.size();
            result.erase(it);
        }
    }

    return result;
}

std::size_t PromptCompressor::estimate_tokens(
    const std::vector<ChatMessage>& messages) {
    std::size_t chars = 0;
    for (const auto& msg : messages) {
        chars += msg.content.size();
        for (const auto& tc : msg.tool_calls) {
            chars += tc.arguments.size() + tc.name.size();
        }
    }
    return chars / 4;  // ~4 chars per token for English
}

std::string PromptCompressor::truncate_tool_result(const std::string& content) const {
    if (content.size() <= config_.max_tool_result_chars) {
        return content;
    }

    // Keep first portion + "... (truncated N chars)"
    const std::size_t keep = config_.max_tool_result_chars - 40;
    return content.substr(0, keep) + "\n... (truncated " +
           std::to_string(content.size() - keep) + " chars)";
}

std::vector<ChatMessage> PromptCompressor::trim_history(
    const std::vector<ChatMessage>& messages) const {

    if (messages.size() <= config_.max_history_messages) {
        return messages;
    }

    // Always keep system message(s) + last N messages
    std::vector<ChatMessage> result;

    // Collect system messages
    for (const auto& msg : messages) {
        if (msg.role == ChatRole::system) {
            result.push_back(msg);
        }
    }

    // Keep last N non-system messages
    const std::size_t keep = config_.max_history_messages - result.size();
    if (messages.size() > keep) {
        const std::size_t start = messages.size() - keep;
        for (std::size_t i = start; i < messages.size(); ++i) {
            if (messages[i].role != ChatRole::system) {
                result.push_back(messages[i]);
            }
        }
    }

    return result;
}
