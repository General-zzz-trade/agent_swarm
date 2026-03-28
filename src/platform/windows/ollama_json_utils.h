#ifndef PLATFORM_WINDOWS_OLLAMA_JSON_UTILS_H
#define PLATFORM_WINDOWS_OLLAMA_JSON_UTILS_H

#include <string>

namespace ollama_json {

std::string escape_json_string(const std::string& value);
std::string extract_top_level_json_string_field(const std::string& json,
                                                const std::string& key);

}  // namespace ollama_json

#endif
