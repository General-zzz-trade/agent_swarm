#ifndef CORE_CONFIG_AGENT_RUNTIME_CONFIG_H
#define CORE_CONFIG_AGENT_RUNTIME_CONFIG_H

#include <cstddef>

struct AgentRuntimeConfig {
    bool default_debug = false;
    int max_model_steps = 50;              // was 5 — allow long autonomous tasks
    std::size_t history_window = 80;       // was 12 — keep much more context
    std::size_t history_byte_budget = 128000;  // was 16KB — 128KB context budget
};

#endif
