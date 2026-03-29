#include "benchmark_runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "../core/interfaces/http_transport.h"
#include "../core/interfaces/model_client.h"
#include "../core/interfaces/approval_provider.h"
#include "../core/model/chat_message.h"
#include "../core/model/tool_schema.h"
#include "../core/threading/thread_pool.h"
#include "../core/indexing/file_index.h"
#include "../core/indexing/file_prefetch.h"
#include "../agent/agent.h"
#include "../agent/tool.h"
#include "../agent/tool_registry.h"
#include "model_client_factory.h"
#include "static_approval_provider.h"

#ifdef _WIN32
#include "../../src/platform/windows/winhttp_transport.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::duration<double, std::milli>;

std::string get_env(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
}

LatencyResult compute_latency(const std::string& label, std::vector<double>& samples) {
    LatencyResult result;
    result.label = label;
    result.rounds = static_cast<int>(samples.size());
    if (samples.empty()) return result;

    std::sort(samples.begin(), samples.end());
    result.min_ms = samples.front();
    result.max_ms = samples.back();
    result.avg_ms = std::accumulate(samples.begin(), samples.end(), 0.0) /
                    static_cast<double>(samples.size());
    const std::size_t p95_idx = static_cast<std::size_t>(
        std::ceil(0.95 * static_cast<double>(samples.size())) - 1);
    result.p95_ms = samples[std::min(p95_idx, samples.size() - 1)];
    return result;
}

std::string format_ms(double ms) {
    std::ostringstream out;
    if (ms >= 1000.0) {
        out << std::fixed << std::setprecision(1) << (ms / 1000.0) << "s";
    } else {
        out << std::fixed << std::setprecision(1) << ms << "ms";
    }
    return out.str();
}

std::string format_latency(const LatencyResult& r) {
    if (r.rounds == 0) return "N/A";
    std::ostringstream out;
    out << format_ms(r.avg_ms);
    if (r.rounds > 1) {
        out << "  [min=" << format_ms(r.min_ms)
            << " max=" << format_ms(r.max_ms)
            << " p95=" << format_ms(r.p95_ms) << "]";
    }
    return out.str();
}

void print_provider_results(std::ostream& output, const ProviderBenchmark& pb) {
    output << "\n=== Provider: " << pb.provider << " (" << pb.model << ") ===\n";
    output << "  Cold connect:      " << format_latency(pb.cold_connect) << "\n";
    output << "  TTFT (avg/" << pb.ttft.rounds << "):      "
           << format_latency(pb.ttft) << "\n";
    output << "  Total response:    " << format_latency(pb.total_response) << "\n";
    output << "  Token throughput:  " << std::fixed << std::setprecision(1)
           << pb.tokens_per_second << " tok/s\n";
    if (pb.streaming_overhead_pct != 0) {
        output << "  Streaming overhead: "
               << (pb.streaming_overhead_pct >= 0 ? "+" : "")
               << std::fixed << std::setprecision(1)
               << pb.streaming_overhead_pct << "%\n";
    }
}

void print_infra_results(std::ostream& output, const InfrastructureBenchmark& ib) {
    output << "\n=== Infrastructure ===\n";
    output << "  Thread pool (4 tasks):  " << format_latency(ib.pool_parallel)
           << "  vs  async: " << format_latency(ib.async_parallel)
           << "  -> " << std::fixed << std::setprecision(1)
           << ib.pool_speedup << "x faster\n";
    output << "  File index build:       " << format_latency(ib.index_build)
           << "  (" << ib.indexed_files << " files, "
           << (ib.indexed_bytes / 1024) << "KB)\n";
    output << "  Index search \"agent\":   " << format_latency(ib.index_search)
           << "  vs brute: " << format_latency(ib.brute_search)
           << "  -> " << std::fixed << std::setprecision(0)
           << ib.index_speedup << "x faster\n";
    output << "  Prefetch warm read:     " << format_latency(ib.prefetch_warm)
           << "  vs cold: " << format_latency(ib.prefetch_cold)
           << "  -> " << std::fixed << std::setprecision(0)
           << ib.prefetch_speedup << "x faster\n";
}

std::string latency_to_json(const LatencyResult& r) {
    std::ostringstream out;
    out << "{\"label\":\"" << r.label << "\""
        << ",\"min_ms\":" << std::fixed << std::setprecision(2) << r.min_ms
        << ",\"avg_ms\":" << r.avg_ms
        << ",\"max_ms\":" << r.max_ms
        << ",\"p95_ms\":" << r.p95_ms
        << ",\"rounds\":" << r.rounds << "}";
    return out.str();
}

std::string provider_to_json(const ProviderBenchmark& pb) {
    std::ostringstream out;
    out << "{\"provider\":\"" << pb.provider << "\""
        << ",\"model\":\"" << pb.model << "\""
        << ",\"cold_connect\":" << latency_to_json(pb.cold_connect)
        << ",\"ttft\":" << latency_to_json(pb.ttft)
        << ",\"total_response\":" << latency_to_json(pb.total_response)
        << ",\"tokens_per_second\":" << std::fixed << std::setprecision(1) << pb.tokens_per_second
        << ",\"streaming_overhead_pct\":" << pb.streaming_overhead_pct
        << "}";
    return out.str();
}

std::string infra_to_json(const InfrastructureBenchmark& ib) {
    std::ostringstream out;
    out << "{\"pool_parallel\":" << latency_to_json(ib.pool_parallel)
        << ",\"async_parallel\":" << latency_to_json(ib.async_parallel)
        << ",\"pool_speedup\":" << std::fixed << std::setprecision(2) << ib.pool_speedup
        << ",\"index_build\":" << latency_to_json(ib.index_build)
        << ",\"index_search\":" << latency_to_json(ib.index_search)
        << ",\"brute_search\":" << latency_to_json(ib.brute_search)
        << ",\"index_speedup\":" << ib.index_speedup
        << ",\"prefetch_warm\":" << latency_to_json(ib.prefetch_warm)
        << ",\"prefetch_cold\":" << latency_to_json(ib.prefetch_cold)
        << ",\"prefetch_speedup\":" << ib.prefetch_speedup
        << ",\"indexed_files\":" << ib.indexed_files
        << ",\"indexed_bytes\":" << ib.indexed_bytes
        << "}";
    return out.str();
}

// --- Provider benchmarks ---

ProviderBenchmark bench_provider(
    const AppConfig& config,
    const std::string& provider,
    int warmup, int rounds) {

    ProviderBenchmark result;
    result.provider = provider;

    // Create transport
    std::shared_ptr<IHttpTransport> transport;
#ifdef _WIN32
    transport = std::make_shared<WinHttpTransport>();
#endif

    // Create client
    AppConfig cfg = config;
    cfg.provider = provider;
    std::unique_ptr<IModelClient> client;
    try {
        client = create_model_client(cfg, "", transport);
        if (!client) {
            result.model = "(legacy ollama - skipped)";
            return result;
        }
    } catch (const std::exception& e) {
        result.model = std::string("ERROR: ") + e.what();
        return result;
    }
    result.model = client->model();

    // Test prompts
    const std::string simple_prompt = "/no_think\nWhat is 2+2? Answer with just the number.";
    const std::string long_prompt = "/no_think\nList exactly 10 popular programming languages, one per line. Just the names, nothing else.";

    // --- Cold connect ---
    {
        std::vector<double> samples;
        const auto cold_start = Clock::now();
        std::vector<ChatMessage> messages = {{ChatRole::user, simple_prompt}};
        try {
            client->chat(messages, {});
        } catch (...) {}
        const double cold_ms = std::chrono::duration_cast<Ms>(Clock::now() - cold_start).count();
        samples.push_back(cold_ms);
        result.cold_connect = compute_latency("cold_connect", samples);
    }

    // --- TTFT (time-to-first-token) + throughput using longer prompt ---
    if (client->supports_streaming()) {
        std::vector<double> ttft_samples;
        std::vector<double> total_samples;
        std::vector<double> token_counts;

        for (int i = -warmup; i < rounds; ++i) {
            std::vector<ChatMessage> messages = {{ChatRole::user, long_prompt}};
            const auto start = Clock::now();
            double first_token_ms = -1;
            int token_count = 0;

            std::size_t total_chars = 0;
            try {
                client->chat_streaming(messages, {},
                    [&](const std::string& token) -> bool {
                        if (first_token_ms < 0 && !token.empty()) {
                            first_token_ms = std::chrono::duration_cast<Ms>(
                                Clock::now() - start).count();
                        }
                        ++token_count;
                        total_chars += token.size();
                        return true;
                    });
            } catch (...) {}

            const double total_ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count();

            // Estimate tokens from characters (~4 chars per token for English)
            const double estimated_tokens = std::max(
                static_cast<double>(token_count),
                static_cast<double>(total_chars) / 4.0);

            if (i >= 0) {  // Skip warmup rounds
                if (first_token_ms >= 0) ttft_samples.push_back(first_token_ms);
                total_samples.push_back(total_ms);
                token_counts.push_back(estimated_tokens);
            }
        }

        result.ttft = compute_latency("ttft", ttft_samples);
        result.total_response = compute_latency("total_response", total_samples);

        // Token throughput
        double total_tokens = std::accumulate(token_counts.begin(), token_counts.end(), 0.0);
        double total_time_s = std::accumulate(total_samples.begin(), total_samples.end(), 0.0) / 1000.0;
        result.tokens_per_second = total_time_s > 0 ? total_tokens / total_time_s : 0;
    }

    // --- Non-streaming total for overhead comparison (same prompt as streaming) ---
    {
        std::vector<double> sync_samples;
        for (int i = -warmup; i < rounds; ++i) {
            std::vector<ChatMessage> messages = {{ChatRole::user, long_prompt}};
            const auto start = Clock::now();
            try {
                client->chat(messages, {});
            } catch (...) {}
            const double ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count();
            if (i >= 0) sync_samples.push_back(ms);
        }

        if (!sync_samples.empty() && result.total_response.avg_ms > 0) {
            const double sync_avg = std::accumulate(sync_samples.begin(), sync_samples.end(), 0.0) /
                                    static_cast<double>(sync_samples.size());
            result.streaming_overhead_pct =
                ((result.total_response.avg_ms - sync_avg) / sync_avg) * 100.0;

            // If we didn't get streaming results, use sync as total
            if (result.total_response.rounds == 0) {
                result.total_response = compute_latency("total_response", sync_samples);
            }
        } else if (!sync_samples.empty()) {
            result.total_response = compute_latency("total_response", sync_samples);
        }
    }

    return result;
}

// --- Infrastructure benchmarks ---

InfrastructureBenchmark bench_infrastructure(
    const std::filesystem::path& workspace, int warmup, int rounds) {

    InfrastructureBenchmark result;

    // --- Thread pool vs std::async ---
    {
        const int task_count = 4;
        const int sleep_ms = 10;

        // Thread pool
        ThreadPool pool(4);
        std::vector<double> pool_samples;
        for (int i = -warmup; i < rounds; ++i) {
            const auto start = Clock::now();
            std::vector<std::future<int>> futures;
            for (int t = 0; t < task_count; ++t) {
                futures.push_back(pool.submit([sleep_ms]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
                    return 1;
                }));
            }
            for (auto& f : futures) f.get();
            const double ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count();
            if (i >= 0) pool_samples.push_back(ms);
        }
        result.pool_parallel = compute_latency("pool_parallel", pool_samples);

        // std::async
        std::vector<double> async_samples;
        for (int i = -warmup; i < rounds; ++i) {
            const auto start = Clock::now();
            std::vector<std::future<int>> futures;
            for (int t = 0; t < task_count; ++t) {
                futures.push_back(std::async(std::launch::async, [sleep_ms]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
                    return 1;
                }));
            }
            for (auto& f : futures) f.get();
            const double ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count();
            if (i >= 0) async_samples.push_back(ms);
        }
        result.async_parallel = compute_latency("async_parallel", async_samples);

        result.pool_speedup = result.pool_parallel.avg_ms > 0
            ? result.async_parallel.avg_ms / result.pool_parallel.avg_ms : 0;
    }

    // --- File index ---
    {
        FileIndex index;

        // Build
        std::vector<double> build_samples;
        for (int i = -warmup; i < rounds; ++i) {
            const auto start = Clock::now();
            index.build(workspace);
            const double ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count();
            if (i >= 0) build_samples.push_back(ms);
        }
        result.index_build = compute_latency("index_build", build_samples);
        result.indexed_files = index.file_count();
        result.indexed_bytes = index.total_bytes();

        // Search with index
        std::vector<double> search_samples;
        for (int i = -warmup; i < rounds * 10; ++i) {
            const auto start = Clock::now();
            index.search("agent", 50);
            const double ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count();
            if (i >= 0) search_samples.push_back(ms);
        }
        result.index_search = compute_latency("index_search", search_samples);

        // Brute force search (use short query to force brute path)
        std::vector<double> brute_samples;
        for (int i = -warmup; i < rounds * 10; ++i) {
            const auto start = Clock::now();
            index.search("ag", 50);  // <3 chars triggers brute force
            const double ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count();
            if (i >= 0) brute_samples.push_back(ms);
        }
        result.brute_search = compute_latency("brute_search", brute_samples);

        result.index_speedup = result.index_search.avg_ms > 0
            ? result.brute_search.avg_ms / result.index_search.avg_ms : 0;
    }

    // --- Prefetch cache ---
    {
        ThreadPool pool(2);
        FilePrefetchCache cache(pool);

        // Find a real file to test with
        std::filesystem::path test_file;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(workspace, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".cpp") {
                test_file = entry.path();
                break;
            }
        }
        if (test_file.empty()) {
            test_file = workspace / "CMakeLists.txt";
        }

        // Cold read (no cache)
        std::vector<double> cold_samples;
        for (int i = -warmup; i < rounds; ++i) {
            const auto start = Clock::now();
            std::ifstream input(test_file, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
            const double ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count();
            if (i >= 0) cold_samples.push_back(ms);
        }
        result.prefetch_cold = compute_latency("prefetch_cold", cold_samples);

        // Warm the cache
        cache.warm(test_file);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // Let prefetch complete

        // Warm read (from cache)
        std::vector<double> warm_samples;
        for (int i = -warmup; i < rounds * 10; ++i) {
            const auto start = Clock::now();
            const std::string content = cache.get(test_file);
            const double ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count();
            if (i >= 0 && !content.empty()) warm_samples.push_back(ms);
        }
        result.prefetch_warm = compute_latency("prefetch_warm", warm_samples);

        result.prefetch_speedup = result.prefetch_warm.avg_ms > 0
            ? result.prefetch_cold.avg_ms / result.prefetch_warm.avg_ms : 0;
    }

    return result;
}

// =======================================================================
// Mock components for framework-only benchmarks (no API key needed)
// =======================================================================

/// Model client that returns instant canned responses.
/// Measures pure C++ overhead of the agent loop without any network/inference.
class MockModelClient : public IModelClient {
public:
    explicit MockModelClient(int simulated_tokens = 50, int token_delay_us = 0)
        : simulated_tokens_(simulated_tokens), token_delay_us_(token_delay_us) {}

    std::string generate(const std::string& /*prompt*/) const override {
        // Return a "reply" action in the legacy JSON format
        return R"({"action":"reply","content":"Mock response with 50 tokens of simulated output text for benchmarking the agent framework overhead.","reason":"benchmark","risk":"low","requires_confirmation":"false"})";
    }

    const std::string& model() const override {
        static const std::string name = "mock-instant";
        return name;
    }

    ChatMessage chat(const std::vector<ChatMessage>& messages,
                     const std::vector<ToolSchema>& tools) const override {
        ChatMessage result;
        result.role = ChatRole::assistant;

        // After a tool result in history, return a reply (end the loop)
        bool has_tool_result = false;
        for (const auto& msg : messages) {
            if (msg.role == ChatRole::tool) has_tool_result = true;
        }

        if (!tools.empty() && !has_tool_result) {
            // First turn: simulate a tool call
            ToolCallRequest tc;
            tc.id = "mock_call_1";
            tc.name = tools[0].name;
            tc.arguments = R"({"args":"benchmark_test"})";
            result.tool_calls.push_back(std::move(tc));
            result.content = "";
        } else {
            // Second turn or no tools: reply
            result.content = "Mock response for benchmark testing.";
        }
        return result;
    }

    ChatMessage chat_streaming(const std::vector<ChatMessage>& messages,
                               const std::vector<ToolSchema>& tools,
                               TokenCallback on_token) const override {
        ChatMessage result = chat(messages, tools);
        if (on_token && !result.content.empty()) {
            // Simulate token-by-token streaming
            const std::string& content = result.content;
            std::size_t pos = 0;
            while (pos < content.size()) {
                const std::size_t chunk_end = std::min(pos + 4, content.size());
                const std::string token = content.substr(pos, chunk_end - pos);
                if (token_delay_us_ > 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(token_delay_us_));
                }
                if (!on_token(token)) break;
                pos = chunk_end;
            }
        }
        return result;
    }

    bool supports_tools() const override { return true; }
    bool supports_streaming() const override { return true; }

private:
    int simulated_tokens_;
    int token_delay_us_;
};

/// Minimal read-only tool for benchmarking tool execution pipeline.
class BenchmarkReadTool : public Tool {
public:
    explicit BenchmarkReadTool(const std::string& tool_name) : name_(tool_name) {}
    std::string name() const override { return name_; }
    std::string description() const override { return "Benchmark tool"; }
    ToolResult run(const std::string& /*args*/) const override {
        return {true, "benchmark_result_ok"};
    }
    bool is_read_only() const override { return true; }
private:
    std::string name_;
};

/// Tool that simulates work (configurable delay).
class BenchmarkSlowTool : public Tool {
public:
    BenchmarkSlowTool(const std::string& tool_name, int work_us)
        : name_(tool_name), work_us_(work_us) {}
    std::string name() const override { return name_; }
    std::string description() const override { return "Slow benchmark tool"; }
    ToolResult run(const std::string& /*args*/) const override {
        // Simulate CPU work instead of sleep for more realistic measurement
        volatile int sum = 0;
        const auto deadline = Clock::now() + std::chrono::microseconds(work_us_);
        while (Clock::now() < deadline) {
            for (int i = 0; i < 100; ++i) sum += i;
        }
        return {true, "slow_result"};
    }
    bool is_read_only() const override { return true; }
private:
    std::string name_;
    int work_us_;
};

// --- Framework benchmark implementation ---

FrameworkBenchmark bench_framework(
    const std::filesystem::path& workspace, int warmup, int rounds) {

    FrameworkBenchmark result;
    using Us = std::chrono::duration<double, std::micro>;

    // --- Agent loop overhead (legacy text path) ---
    {
        std::vector<double> samples;
        for (int i = -warmup; i < rounds; ++i) {
            auto client = std::make_unique<MockModelClient>();
            ToolRegistry tools;
            tools.register_tool(std::make_unique<BenchmarkReadTool>("calculator"));
            auto approval = std::make_shared<StaticApprovalProvider>(true);

            Agent agent(std::move(client), approval, workspace, PolicyConfig{},
                        AgentRuntimeConfig{}, false, nullptr, std::move(tools));

            const auto start = Clock::now();
            agent.run_turn("What is 2+2?");
            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) samples.push_back(us / 1000.0);  // convert to ms
        }
        result.agent_loop_overhead = compute_latency("agent_loop", samples);
    }

    // --- Agent structured path overhead (tool call → tool result → reply) ---
    {
        std::vector<double> samples;
        for (int i = -warmup; i < rounds; ++i) {
            auto client = std::make_unique<MockModelClient>();
            ToolRegistry tools;
            tools.register_tool(std::make_unique<BenchmarkReadTool>("calculator"));
            auto approval = std::make_shared<StaticApprovalProvider>(true);
            AgentRuntimeConfig runtime;
            runtime.max_model_steps = 5;

            Agent agent(std::move(client), approval, workspace, PolicyConfig{},
                        runtime, false, nullptr, std::move(tools));

            const auto start = Clock::now();
            try {
                agent.run_turn("Use calculator to compute 2+2");
            } catch (const std::exception&) {
                // max steps exceeded is ok for benchmark
            }
            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) samples.push_back(us / 1000.0);
        }
        result.agent_structured_overhead = compute_latency("agent_structured", samples);
    }

    // --- JSON serialize / deserialize ---
    {
        // Serialize: build tool schemas JSON
        std::vector<ToolSchema> schemas;
        for (int t = 0; t < 10; ++t) {
            schemas.push_back({"tool_" + std::to_string(t), "Description " + std::to_string(t),
                               {{"arg1", "string", "Argument 1", true},
                                {"arg2", "integer", "Argument 2", false}}});
        }

        std::vector<double> ser_samples;
        for (int i = -warmup; i < rounds * 100; ++i) {
            const auto start = Clock::now();
            nlohmann::json j = nlohmann::json::array();
            for (const auto& schema : schemas) {
                nlohmann::json params = nlohmann::json::object();
                for (const auto& p : schema.parameters) {
                    params[p.name] = {{"type", p.type}, {"description", p.description}};
                }
                j.push_back({{"type", "function"},
                             {"function", {{"name", schema.name},
                                           {"description", schema.description},
                                           {"parameters", {{"type", "object"},
                                                           {"properties", params}}}}}});
            }
            std::string output = j.dump();
            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) ser_samples.push_back(us / 1000.0);
        }
        result.json_serialize = compute_latency("json_serialize", ser_samples);

        // Deserialize: parse a typical chat response
        const std::string response_json = R"({
            "id": "chatcmpl-123",
            "choices": [{
                "message": {
                    "role": "assistant",
                    "content": null,
                    "tool_calls": [{
                        "id": "call_abc",
                        "type": "function",
                        "function": {
                            "name": "read_file",
                            "arguments": "{\"path\":\"src/main.cpp\"}"
                        }
                    }]
                }
            }]
        })";

        std::vector<double> deser_samples;
        for (int i = -warmup; i < rounds * 100; ++i) {
            const auto start = Clock::now();
            auto j = nlohmann::json::parse(response_json);
            const auto& msg = j["choices"][0]["message"];
            ChatMessage cm;
            cm.role = ChatRole::assistant;
            if (msg.contains("content") && !msg["content"].is_null()) {
                cm.content = msg["content"].get<std::string>();
            }
            if (msg.contains("tool_calls")) {
                for (const auto& tc : msg["tool_calls"]) {
                    ToolCallRequest call;
                    call.id = tc["id"].get<std::string>();
                    call.name = tc["function"]["name"].get<std::string>();
                    call.arguments = tc["function"]["arguments"].get<std::string>();
                    cm.tool_calls.push_back(std::move(call));
                }
            }
            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) deser_samples.push_back(us / 1000.0);
        }
        result.json_deserialize = compute_latency("json_deserialize", deser_samples);
    }

    // --- Tool lookup (O(1) hash vs would-be O(n)) ---
    {
        ToolRegistry registry;
        for (int t = 0; t < 50; ++t) {
            registry.register_tool(
                std::make_unique<BenchmarkReadTool>("tool_" + std::to_string(t)));
        }

        std::vector<double> samples;
        for (int i = -warmup; i < rounds * 100; ++i) {
            const auto start = Clock::now();
            // 1000 lookups
            for (int k = 0; k < 1000; ++k) {
                const Tool* t = registry.find("tool_" + std::to_string(k % 50));
                (void)t;
            }
            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) samples.push_back(us / 1000.0);
        }
        result.tool_lookup = compute_latency("tool_lookup_1000x", samples);
    }

    // --- Tool execution: single ---
    {
        BenchmarkReadTool tool("bench_tool");
        std::vector<double> samples;
        for (int i = -warmup; i < rounds * 1000; ++i) {
            const auto start = Clock::now();
            tool.run("test_args");
            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) samples.push_back(us / 1000.0);
        }
        result.tool_execute_single = compute_latency("tool_single", samples);
    }

    // --- Tool batch: pool vs sequential (with simulated 500us CPU work per tool) ---
    {
        const int task_count = 8;
        const int work_us = 500;

        ThreadPool pool(4);

        // Pool parallel
        std::vector<double> pool_samples;
        for (int i = -warmup; i < rounds; ++i) {
            const auto start = Clock::now();
            std::vector<std::future<ToolResult>> futures;
            for (int t = 0; t < task_count; ++t) {
                futures.push_back(pool.submit([work_us]() {
                    BenchmarkSlowTool tool("slow", work_us);
                    return tool.run("test");
                }));
            }
            for (auto& f : futures) f.get();
            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) pool_samples.push_back(us / 1000.0);
        }
        result.tool_execute_batch_pool = compute_latency("batch_pool", pool_samples);

        // Sequential
        std::vector<double> seq_samples;
        for (int i = -warmup; i < rounds; ++i) {
            const auto start = Clock::now();
            for (int t = 0; t < task_count; ++t) {
                BenchmarkSlowTool tool("slow", work_us);
                tool.run("test");
            }
            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) seq_samples.push_back(us / 1000.0);
        }
        result.tool_execute_batch_seq = compute_latency("batch_seq", seq_samples);

        result.batch_speedup = result.tool_execute_batch_pool.avg_ms > 0
            ? result.tool_execute_batch_seq.avg_ms / result.tool_execute_batch_pool.avg_ms : 0;
    }

    // --- Streaming callback overhead ---
    {
        std::vector<double> samples;
        const std::string fake_token = "hello";
        for (int i = -warmup; i < rounds * 100; ++i) {
            std::string accumulated;
            int count = 0;
            const auto start = Clock::now();
            // Simulate 100 token callbacks
            for (int t = 0; t < 100; ++t) {
                accumulated += fake_token;
                ++count;
            }
            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) samples.push_back(us / 1000.0);
        }
        result.streaming_callback = compute_latency("streaming_100tok", samples);
    }

    // --- SSE event formatting ---
    {
        std::vector<double> samples;
        const std::string token = "Hello world token";
        for (int i = -warmup; i < rounds * 100; ++i) {
            const auto start = Clock::now();
            // Format 100 SSE events
            for (int t = 0; t < 100; ++t) {
                std::string sse = "data: ";
                // Escape for JSON
                for (char ch : token) {
                    if (ch == '"') sse += "\\\"";
                    else if (ch == '\\') sse += "\\\\";
                    else if (ch == '\n') sse += "\\n";
                    else sse += ch;
                }
                sse += "\n\n";
                (void)sse.size();  // Prevent optimization
            }
            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) samples.push_back(us / 1000.0);
        }
        result.sse_format = compute_latency("sse_format_100", samples);
    }

    // --- HTTP localhost roundtrip (Winsock echo server) ---
#ifdef _WIN32
    {
        // Start a tiny TCP echo server on a random port
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);

        SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = 0;  // OS picks port
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(server_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(server_sock, 5);

        // Get assigned port
        sockaddr_in bound{};
        int len = sizeof(bound);
        getsockname(server_sock, reinterpret_cast<sockaddr*>(&bound), &len);
        const int port = ntohs(bound.sin_port);

        // Echo server thread: accept, read, respond with HTTP 200, close
        std::atomic<bool> server_running{true};
        std::thread server_thread([&]() {
            while (server_running.load()) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(server_sock, &fds);
                timeval tv{0, 100000};  // 100ms timeout
                if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;

                SOCKET client = accept(server_sock, nullptr, nullptr);
                if (client == INVALID_SOCKET) continue;

                // Read request (up to 4KB)
                char buf[4096];
                recv(client, buf, sizeof(buf), 0);

                // Send HTTP response
                const char* response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: 15\r\n"
                    "Connection: close\r\n\r\n"
                    "{\"ok\":true}   ";
                send(client, response, static_cast<int>(strlen(response)), 0);
                closesocket(client);
            }
        });

        // Benchmark: HTTP POST to localhost
        std::vector<double> samples;
        for (int i = -warmup; i < rounds * 10; ++i) {
            const auto start = Clock::now();

            SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in target{};
            target.sin_family = AF_INET;
            target.sin_port = htons(static_cast<u_short>(port));
            target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            connect(sock, reinterpret_cast<sockaddr*>(&target), sizeof(target));

            const char* req =
                "POST /api/test HTTP/1.1\r\n"
                "Host: 127.0.0.1\r\n"
                "Content-Length: 2\r\n\r\n{}";
            send(sock, req, static_cast<int>(strlen(req)), 0);

            char resp[4096];
            recv(sock, resp, sizeof(resp), 0);
            closesocket(sock);

            const double us = std::chrono::duration_cast<Us>(Clock::now() - start).count();
            if (i >= 0) samples.push_back(us / 1000.0);
        }
        result.http_localhost_roundtrip = compute_latency("http_roundtrip", samples);

        // Cleanup
        server_running.store(false);
        server_thread.join();
        closesocket(server_sock);
        WSACleanup();
    }
#endif

    return result;
}

void print_framework_results(std::ostream& output, const FrameworkBenchmark& fb) {
    output << "\n=== Framework Overhead (Mock Model, No Network) ===\n";
    output << "  Agent loop (legacy path):   " << format_latency(fb.agent_loop_overhead) << "\n";
    output << "  Agent loop (structured):    " << format_latency(fb.agent_structured_overhead) << "\n";

    output << "\n=== JSON Processing ===\n";
    output << "  Serialize (10 tool schemas): " << format_latency(fb.json_serialize) << "\n";
    output << "  Deserialize (chat response): " << format_latency(fb.json_deserialize) << "\n";

    output << "\n=== Tool Pipeline ===\n";
    output << "  Registry lookup (1000x):    " << format_latency(fb.tool_lookup) << "\n";
    output << "  Single tool execute:        " << format_latency(fb.tool_execute_single) << "\n";
    output << "  8 tools parallel (pool):    " << format_latency(fb.tool_execute_batch_pool) << "\n";
    output << "  8 tools sequential:         " << format_latency(fb.tool_execute_batch_seq)
           << "  -> " << std::fixed << std::setprecision(1) << fb.batch_speedup << "x speedup\n";

    output << "\n=== Streaming Pipeline ===\n";
    output << "  100 token callbacks:        " << format_latency(fb.streaming_callback) << "\n";
    output << "  100 SSE event format:       " << format_latency(fb.sse_format) << "\n";

    output << "\n=== Network (localhost) ===\n";
    output << "  HTTP POST roundtrip:        " << format_latency(fb.http_localhost_roundtrip) << "\n";
}

std::string framework_to_json(const FrameworkBenchmark& fb) {
    std::ostringstream out;
    out << "{\"agent_loop\":" << latency_to_json(fb.agent_loop_overhead)
        << ",\"agent_structured\":" << latency_to_json(fb.agent_structured_overhead)
        << ",\"json_serialize\":" << latency_to_json(fb.json_serialize)
        << ",\"json_deserialize\":" << latency_to_json(fb.json_deserialize)
        << ",\"tool_lookup\":" << latency_to_json(fb.tool_lookup)
        << ",\"tool_single\":" << latency_to_json(fb.tool_execute_single)
        << ",\"batch_pool\":" << latency_to_json(fb.tool_execute_batch_pool)
        << ",\"batch_seq\":" << latency_to_json(fb.tool_execute_batch_seq)
        << ",\"batch_speedup\":" << std::fixed << std::setprecision(2) << fb.batch_speedup
        << ",\"streaming_cb\":" << latency_to_json(fb.streaming_callback)
        << ",\"sse_format\":" << latency_to_json(fb.sse_format)
        << ",\"http_roundtrip\":" << latency_to_json(fb.http_localhost_roundtrip)
        << "}";
    return out.str();
}

std::vector<std::string> detect_available_providers() {
    std::vector<std::string> providers;

    // Check Ollama reachability (try to connect)
    // For simplicity, always include ollama-chat as an option
    providers.push_back("ollama-chat");

    if (!get_env("OPENAI_API_KEY").empty()) {
        providers.push_back("openai");
    }
    if (!get_env("ANTHROPIC_API_KEY").empty()) {
        providers.push_back("claude");
    }
    if (!get_env("GEMINI_API_KEY").empty()) {
        providers.push_back("gemini");
    }
    return providers;
}

}  // namespace

BenchmarkConfig resolve_bench_config(const std::vector<std::string>& args) {
    BenchmarkConfig config;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--provider" && i + 1 < args.size()) {
            config.provider = args[++i];
        } else if (args[i] == "--rounds" && i + 1 < args.size()) {
            config.rounds = std::stoi(args[++i]);
        } else if (args[i] == "--warmup" && i + 1 < args.size()) {
            config.warmup = std::stoi(args[++i]);
        } else if (args[i] == "--json") {
            config.json_output = true;
        }
    }
    return config;
}

void run_benchmarks(const AppConfig& config,
                    const std::filesystem::path& workspace,
                    const BenchmarkConfig& bench_config,
                    std::ostream& output) {

    // Determine which providers to benchmark
    std::vector<std::string> providers;
    if (!bench_config.provider.empty()) {
        providers.push_back(bench_config.provider);
    } else {
        providers = detect_available_providers();
    }

    output << "Mini NN Performance Benchmark\n";
    output << "Rounds: " << bench_config.rounds
           << "  Warmup: " << bench_config.warmup << "\n";
    output << "Workspace: " << workspace.string() << "\n";

    // Run provider benchmarks
    std::vector<ProviderBenchmark> provider_results;
    for (const auto& provider : providers) {
        output << "\nBenchmarking provider: " << provider << "..." << std::flush;
        auto pb = bench_provider(config, provider,
                                  bench_config.warmup, bench_config.rounds);
        provider_results.push_back(std::move(pb));
        output << " done.\n";
    }

    // Run framework benchmarks (always, no API needed)
    output << "\nBenchmarking framework overhead..." << std::flush;
    auto framework = bench_framework(workspace, bench_config.warmup, bench_config.rounds);
    output << " done.\n";

    // Run infrastructure benchmarks
    output << "Benchmarking infrastructure..." << std::flush;
    auto infra = bench_infrastructure(workspace,
                                      bench_config.warmup, bench_config.rounds);
    output << " done.\n";

    // Output results
    if (bench_config.json_output) {
        std::ostringstream json;
        json << "{\"providers\":[";
        for (std::size_t i = 0; i < provider_results.size(); ++i) {
            if (i > 0) json << ",";
            json << provider_to_json(provider_results[i]);
        }
        json << "],\"framework\":" << framework_to_json(framework)
             << ",\"infrastructure\":" << infra_to_json(infra) << "}";
        output << json.str() << "\n";
    } else {
        output << "\n========================================\n";
        output << "          BENCHMARK RESULTS\n";
        output << "========================================\n";

        for (const auto& pb : provider_results) {
            print_provider_results(output, pb);
        }
        print_framework_results(output, framework);
        print_infra_results(output, infra);
        output << "\n";
    }
}
