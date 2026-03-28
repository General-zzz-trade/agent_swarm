#include "action_parser.h"

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string to_lower_copy(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool parse_bool_field(const std::unordered_map<std::string, std::string>& fields,
                      const std::string& key) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return false;
    }

    const std::string value = to_lower_copy(trim(it->second));
    return value == "true" || value == "1" || value == "yes";
}

std::string parse_string_field(const std::unordered_map<std::string, std::string>& fields,
                               const std::string& key) {
    const auto it = fields.find(key);
    return it == fields.end() ? "" : trim(it->second);
}

std::string extract_json_object(const std::string& text) {
    bool in_string = false;
    bool escaped = false;
    int brace_depth = 0;
    std::size_t start = std::string::npos;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char current = text[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (current == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (current == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (current == '{') {
            if (brace_depth == 0) {
                start = i;
            }
            ++brace_depth;
        } else if (current == '}') {
            --brace_depth;
            if (brace_depth == 0 && start != std::string::npos) {
                return text.substr(start, i - start + 1);
            }
            if (brace_depth < 0) {
                break;
            }
        }
    }

    throw std::runtime_error("Model response did not contain a valid JSON object");
}

class JsonCursor {
public:
    explicit JsonCursor(std::string json) : json_(std::move(json)), position_(0) {}

    std::unordered_map<std::string, std::string> parse_object() {
        skip_whitespace();
        expect('{');

        std::unordered_map<std::string, std::string> fields;
        skip_whitespace();
        if (match('}')) {
            return fields;
        }

        while (true) {
            skip_whitespace();
            const std::string key = parse_string();
            skip_whitespace();
            expect(':');
            skip_whitespace();
            fields[key] = parse_string();
            skip_whitespace();

            if (match('}')) {
                break;
            }
            expect(',');
        }

        skip_whitespace();
        if (position_ != json_.size()) {
            throw std::runtime_error("Unexpected trailing data after JSON object");
        }

        return fields;
    }

private:
    std::string json_;
    std::size_t position_;

    static void append_utf8(std::string* value, std::uint32_t codepoint) {
        if (codepoint > 0x10FFFF) {
            throw std::runtime_error("Unicode codepoint out of range");
        }
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
            throw std::runtime_error("Invalid standalone unicode surrogate");
        }

        if (codepoint <= 0x7F) {
            value->push_back(static_cast<char>(codepoint));
            return;
        }
        if (codepoint <= 0x7FF) {
            value->push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            value->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            return;
        }
        if (codepoint <= 0xFFFF) {
            value->push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            value->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            value->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            return;
        }

        value->push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        value->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        value->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        value->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }

    static unsigned int decode_hex_digit(char hex) {
        if (hex >= '0' && hex <= '9') {
            return static_cast<unsigned int>(hex - '0');
        }
        if (hex >= 'a' && hex <= 'f') {
            return static_cast<unsigned int>(hex - 'a' + 10);
        }
        if (hex >= 'A' && hex <= 'F') {
            return static_cast<unsigned int>(hex - 'A' + 10);
        }
        throw std::runtime_error("Invalid unicode escape");
    }

    void skip_whitespace() {
        while (position_ < json_.size() &&
               std::isspace(static_cast<unsigned char>(json_[position_]))) {
            ++position_;
        }
    }

    bool match(char expected) {
        if (position_ < json_.size() && json_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!match(expected)) {
            throw std::runtime_error(std::string("Expected '") + expected + "'");
        }
    }

    std::uint32_t parse_unicode_escape() {
        if (position_ + 4 > json_.size()) {
            throw std::runtime_error("Invalid unicode escape");
        }

        std::uint32_t codepoint = 0;
        for (int i = 0; i < 4; ++i) {
            codepoint <<= 4;
            codepoint += decode_hex_digit(json_[position_++]);
        }
        return codepoint;
    }

    std::string parse_string() {
        expect('"');

        std::string value;
        while (position_ < json_.size()) {
            const char current = json_[position_++];
            if (current == '"') {
                return value;
            }
            if (current != '\\') {
                value += current;
                continue;
            }

            if (position_ >= json_.size()) {
                throw std::runtime_error("Invalid JSON string escape");
            }

            const char escaped = json_[position_++];
            switch (escaped) {
                case '"':
                    value += '"';
                    break;
                case '\\':
                    value += '\\';
                    break;
                case '/':
                    value += '/';
                    break;
                case 'b':
                    value += '\b';
                    break;
                case 'f':
                    value += '\f';
                    break;
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                case 'u': {
                    std::uint32_t codepoint = parse_unicode_escape();
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                        if (position_ + 6 > json_.size() || json_[position_] != '\\' ||
                            json_[position_ + 1] != 'u') {
                            throw std::runtime_error("Invalid unicode surrogate pair");
                        }
                        position_ += 2;
                        const std::uint32_t low_surrogate = parse_unicode_escape();
                        if (low_surrogate < 0xDC00 || low_surrogate > 0xDFFF) {
                            throw std::runtime_error("Invalid unicode surrogate pair");
                        }
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) +
                                    (low_surrogate - 0xDC00);
                    } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                        throw std::runtime_error("Invalid standalone unicode surrogate");
                    }
                    append_utf8(&value, codepoint);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported JSON escape");
            }
        }

        throw std::runtime_error("Unterminated JSON string");
    }
};

}  // namespace

Action parse_action_response(const std::string& response) {
    const std::string json_object = extract_json_object(response);
    const auto fields = JsonCursor(json_object).parse_object();

    const auto action_it = fields.find("action");
    if (action_it == fields.end()) {
        throw std::runtime_error("Model response is missing the 'action' field");
    }

    const std::string action = trim(action_it->second);
    if (action == "reply") {
        Action result{ActionType::reply, "", "", ""};
        const auto content_it = fields.find("content");
        if (content_it != fields.end()) {
            result.content = content_it->second;
        }
        result.reason = parse_string_field(fields, "reason");
        result.risk = to_lower_copy(parse_string_field(fields, "risk"));
        result.requires_confirmation = parse_bool_field(fields, "requires_confirmation");
        return result;
    }

    if (action == "tool") {
        const auto tool_it = fields.find("tool");
        if (tool_it == fields.end() || trim(tool_it->second).empty()) {
            throw std::runtime_error("Tool action is missing the 'tool' field");
        }

        Action result{ActionType::tool, trim(tool_it->second), "", ""};
        const auto args_it = fields.find("args");
        if (args_it != fields.end()) {
            result.args = args_it->second;
        }
        result.reason = parse_string_field(fields, "reason");
        result.risk = to_lower_copy(parse_string_field(fields, "risk"));
        result.requires_confirmation = parse_bool_field(fields, "requires_confirmation");
        return result;
    }

    throw std::runtime_error("Unsupported action type: " + action);
}
