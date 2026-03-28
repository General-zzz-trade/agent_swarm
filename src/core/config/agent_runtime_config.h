#ifndef CORE_CONFIG_AGENT_RUNTIME_CONFIG_H
#define CORE_CONFIG_AGENT_RUNTIME_CONFIG_H

#include <cstddef>

struct AgentRuntimeConfig {
    bool default_debug = false;
    int max_model_steps = 5;
    std::size_t history_window = 12;
    std::size_t history_byte_budget = 16000;
};

#endif
