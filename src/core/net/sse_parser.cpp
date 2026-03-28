#include "sse_parser.h"

SseParser::SseParser(EventCallback on_event)
    : on_event_(std::move(on_event)) {}

bool SseParser::feed(const std::string& chunk) {
    buffer_ += chunk;

    std::string::size_type pos = 0;
    while (pos < buffer_.size()) {
        const auto nl = buffer_.find('\n', pos);
        if (nl == std::string::npos) {
            break;
        }

        std::string line = buffer_.substr(pos, nl - pos);
        pos = nl + 1;

        // Strip trailing CR
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (!process_line(line)) {
            buffer_.erase(0, pos);
            return false;
        }
    }

    buffer_.erase(0, pos);
    return true;
}

void SseParser::finish() {
    if (has_data_) {
        dispatch_event();
    }
}

bool SseParser::process_line(const std::string& line) {
    // Empty line = dispatch event
    if (line.empty()) {
        if (has_data_) {
            return dispatch_event();
        }
        return true;
    }

    // Comment lines (starting with ':')
    if (line[0] == ':') {
        return true;
    }

    std::string field;
    std::string value;

    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        field = line;
    } else {
        field = line.substr(0, colon);
        value = line.substr(colon + 1);
        // Strip single leading space after colon
        if (!value.empty() && value[0] == ' ') {
            value.erase(0, 1);
        }
    }

    if (field == "event") {
        current_event_type_ = value;
    } else if (field == "data") {
        if (has_data_) {
            current_data_ += '\n';
        }
        current_data_ += value;
        has_data_ = true;
    }
    // Ignore "id", "retry", and unknown fields

    return true;
}

bool SseParser::dispatch_event() {
    Event event;
    event.type = current_event_type_;
    event.data = current_data_;

    current_event_type_.clear();
    current_data_.clear();
    has_data_ = false;

    return on_event_(event);
}
