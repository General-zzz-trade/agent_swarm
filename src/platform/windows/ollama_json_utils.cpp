#include "ollama_json_utils.h"

#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string unescape_json_string(const std::string& value) {
    std::string result;
    result.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\') {
            result += value[i];
            continue;
        }

        if (i + 1 >= value.size()) {
            break;
        }

        const char next = value[++i];
        switch (next) {
            case '\\':
                result += '\\';
                break;
            case '"':
                result += '"';
                break;
            case '/':
                result += '/';
                break;
            case 'n':
                result += '\n';
                break;
            case 'r':
                result += '\r';
                break;
            case 't':
                result += '\t';
                break;
            case 'b':
                result += '\b';
                break;
            case 'f':
                result += '\f';
                break;
            case 'u': {
                if (i + 4 >= value.size()) {
                    throw std::runtime_error("Invalid unicode escape in JSON string");
                }

                unsigned int codepoint = 0;
                for (int hex_index = 0; hex_index < 4; ++hex_index) {
                    const char hex = value[i + 1 + hex_index];
                    codepoint <<= 4;
                    if (hex >= '0' && hex <= '9') {
                        codepoint += static_cast<unsigned int>(hex - '0');
                    } else if (hex >= 'a' && hex <= 'f') {
                        codepoint += static_cast<unsigned int>(hex - 'a' + 10);
                    } else if (hex >= 'A' && hex <= 'F') {
                        codepoint += static_cast<unsigned int>(hex - 'A' + 10);
                    } else {
                        throw std::runtime_error("Invalid unicode escape in JSON string");
                    }
                }

                i += 4;
                if (codepoint <= 0x7F) {
                    result += static_cast<char>(codepoint);
                }
                break;
            }
            default:
                throw std::runtime_error("Unsupported JSON escape sequence");
        }
    }

    return result;
}

void skip_json_whitespace(const std::string& json, std::size_t* position) {
    while (*position < json.size()) {
        const char ch = json[*position];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        ++(*position);
    }
}

std::string parse_json_string(const std::string& json, std::size_t* position) {
    if (*position >= json.size() || json[*position] != '"') {
        throw std::runtime_error("Expected JSON string");
    }

    ++(*position);
    std::string raw_value;
    bool escaped = false;

    while (*position < json.size()) {
        const char current = json[*position];
        ++(*position);

        if (escaped) {
            raw_value += '\\';
            raw_value += current;
            escaped = false;
            continue;
        }
        if (current == '\\') {
            escaped = true;
            continue;
        }
        if (current == '"') {
            return unescape_json_string(raw_value);
        }
        raw_value += current;
    }

    throw std::runtime_error("Unterminated JSON string");
}

void skip_json_value(const std::string& json, std::size_t* position) {
    skip_json_whitespace(json, position);
    if (*position >= json.size()) {
        throw std::runtime_error("Unexpected end of JSON");
    }

    const char current = json[*position];
    if (current == '"') {
        (void)parse_json_string(json, position);
        return;
    }

    if (current == '{' || current == '[') {
        const char open = current;
        const char close = current == '{' ? '}' : ']';
        ++(*position);

        int depth = 1;
        bool in_string = false;
        bool escaped = false;
        while (*position < json.size() && depth > 0) {
            const char ch = json[*position];
            ++(*position);

            if (in_string) {
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (ch == '\\') {
                    escaped = true;
                    continue;
                }
                if (ch == '"') {
                    in_string = false;
                }
                continue;
            }

            if (ch == '"') {
                in_string = true;
                continue;
            }
            if (ch == open) {
                ++depth;
                continue;
            }
            if (ch == close) {
                --depth;
            }
        }

        if (depth != 0) {
            throw std::runtime_error("Unterminated JSON container");
        }
        return;
    }

    while (*position < json.size()) {
        const char ch = json[*position];
        if (ch == ',' || ch == '}' || ch == ']') {
            break;
        }
        ++(*position);
    }
}

}  // namespace

namespace ollama_json {

std::string escape_json_string(const std::string& value) {
    std::ostringstream escaped;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\\':
                escaped << "\\\\";
                break;
            case '"':
                escaped << "\\\"";
                break;
            case '\n':
                escaped << "\\n";
                break;
            case '\r':
                escaped << "\\r";
                break;
            case '\t':
                escaped << "\\t";
                break;
            default:
                escaped << ch;
                break;
        }
    }
    return escaped.str();
}

std::string extract_top_level_json_string_field(const std::string& json,
                                                const std::string& key) {
    std::size_t position = 0;
    skip_json_whitespace(json, &position);
    if (position >= json.size() || json[position] != '{') {
        throw std::runtime_error("Model response is not a JSON object");
    }

    ++position;
    while (true) {
        skip_json_whitespace(json, &position);
        if (position >= json.size()) {
            throw std::runtime_error("Unexpected end of JSON object");
        }
        if (json[position] == '}') {
            return "";
        }

        const std::string current_key = parse_json_string(json, &position);
        skip_json_whitespace(json, &position);
        if (position >= json.size() || json[position] != ':') {
            throw std::runtime_error("Expected ':' after JSON object key");
        }
        ++position;
        skip_json_whitespace(json, &position);

        if (current_key == key) {
            if (position >= json.size() || json[position] != '"') {
                return "";
            }
            return parse_json_string(json, &position);
        }

        skip_json_value(json, &position);
        skip_json_whitespace(json, &position);
        if (position < json.size() && json[position] == ',') {
            ++position;
            continue;
        }
        if (position < json.size() && json[position] == '}') {
            return "";
        }
    }
}

}  // namespace ollama_json
