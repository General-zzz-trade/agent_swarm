#ifndef AGENT_OBSERVATION_H
#define AGENT_OBSERVATION_H

#include <string>

struct Observation {
    bool success = false;
    std::string channel;
    std::string content;
};

#endif
