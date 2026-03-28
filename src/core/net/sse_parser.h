#ifndef CORE_NET_SSE_PARSER_H
#define CORE_NET_SSE_PARSER_H

#include <functional>
#include <string>

// Stateful Server-Sent Events parser.
// Feed raw chunks via feed(); the on_event callback fires for each complete event.
class SseParser {
public:
    struct Event {
        std::string type;   // "event:" value, empty if not specified
        std::string data;   // concatenated "data:" lines (joined with '\n')
    };

    using EventCallback = std::function<bool(const Event& event)>;

    explicit SseParser(EventCallback on_event);

    // Feed a raw chunk from the HTTP stream. Returns false if callback returned false.
    bool feed(const std::string& chunk);

    // Signal end-of-stream; flushes any pending event.
    void finish();

private:
    EventCallback on_event_;
    std::string buffer_;
    std::string current_event_type_;
    std::string current_data_;
    bool has_data_ = false;

    bool process_line(const std::string& line);
    bool dispatch_event();
};

#endif
