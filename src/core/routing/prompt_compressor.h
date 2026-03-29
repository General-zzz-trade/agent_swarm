#ifndef CORE_ROUTING_PROMPT_COMPRESSOR_H
#define CORE_ROUTING_PROMPT_COMPRESSOR_H

#include <string>
#include <vector>

#include "../model/chat_message.h"

/// Compresses chat history to minimize token count for faster inference.
///
/// Groq-speed principle: fewer input tokens = faster TTFT + lower cost.
/// Techniques:
/// 1. Truncate old tool results to summaries
/// 2. Remove redundant system instructions after first turn
/// 3. Collapse consecutive tool results into compact format
/// 4. Trim large file contents to relevant sections
class PromptCompressor {
public:
    struct Config {
        std::size_t max_tool_result_chars = 0;        // 0 = no truncation (lossless)
        std::size_t max_history_messages = 40;        // Keep last N messages
        std::size_t max_total_chars = 0;              // 0 = no cap (lossless)
        bool collapse_tool_results = false;           // Don't merge (lossless)
        bool deduplicate_whitespace = true;           // Remove redundant blank lines
    };

    PromptCompressor();
    explicit PromptCompressor(Config config);

    /// Compress a message history for sending to the model.
    std::vector<ChatMessage> compress(const std::vector<ChatMessage>& messages) const;

    /// Estimate token count (~4 chars per token for English).
    static std::size_t estimate_tokens(const std::vector<ChatMessage>& messages);

private:
    Config config_;

    std::string truncate_tool_result(const std::string& content) const;
    std::vector<ChatMessage> trim_history(const std::vector<ChatMessage>& messages) const;
};

#endif
