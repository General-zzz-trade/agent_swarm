#ifndef APP_BENCHMARK_RUNNER_H
#define APP_BENCHMARK_RUNNER_H

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include "app_config.h"

struct BenchmarkConfig {
    std::string provider;   // empty = auto-detect all available
    int rounds = 5;
    int warmup = 1;
    bool json_output = false;
};

struct LatencyResult {
    std::string label;
    double min_ms = 0;
    double avg_ms = 0;
    double max_ms = 0;
    double p95_ms = 0;
    int rounds = 0;
};

struct ProviderBenchmark {
    std::string provider;
    std::string model;
    LatencyResult cold_connect;
    LatencyResult ttft;
    LatencyResult total_response;
    double tokens_per_second = 0;
    double streaming_overhead_pct = 0;
};

struct InfrastructureBenchmark {
    LatencyResult pool_parallel;
    LatencyResult async_parallel;
    double pool_speedup = 0;
    LatencyResult index_build;
    LatencyResult index_search;
    LatencyResult brute_search;
    double index_speedup = 0;
    LatencyResult prefetch_warm;
    LatencyResult prefetch_cold;
    double prefetch_speedup = 0;
    std::size_t indexed_files = 0;
    std::size_t indexed_bytes = 0;
};

/// Framework-only benchmarks using mock model client (no API/network needed).
struct FrameworkBenchmark {
    // Agent loop overhead (mock model, instant response)
    LatencyResult agent_loop_overhead;       // full run_turn() with mock
    LatencyResult agent_structured_overhead;  // run_turn() structured path
    LatencyResult prompt_build;               // build_prompt() only
    LatencyResult chat_message_build;         // build_chat_messages() only

    // JSON serialization/deserialization
    LatencyResult json_serialize;             // tool schemas → JSON
    LatencyResult json_deserialize;           // JSON → ChatMessage

    // Tool pipeline
    LatencyResult tool_lookup;                // registry.find() ×1000
    LatencyResult tool_execute_single;        // single tool execution
    LatencyResult tool_execute_batch_pool;    // 4 tools parallel (pool)
    LatencyResult tool_execute_batch_seq;     // 4 tools sequential
    double batch_speedup = 0;

    // Streaming pipeline
    LatencyResult streaming_callback;         // token callback overhead
    LatencyResult sse_format;                 // SSE event formatting

    // HTTP transport (local echo)
    LatencyResult http_localhost_roundtrip;   // loopback HTTP POST
};

BenchmarkConfig resolve_bench_config(const std::vector<std::string>& args);

void run_benchmarks(const AppConfig& config,
                    const std::filesystem::path& workspace,
                    const BenchmarkConfig& bench_config,
                    std::ostream& output);

#endif
