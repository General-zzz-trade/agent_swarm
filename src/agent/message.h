#ifndef AGENT_MESSAGE_H
#define AGENT_MESSAGE_H

#include <string>

struct Message {
    std::string role;
    std::string content;
    std::string name;
};

#endif
