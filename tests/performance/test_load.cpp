// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <filesystem>

#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/gateway/gateway_client.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/config.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/plugins/plugin_system.hpp"
#include "../test_helpers.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

namespace quantclaw::gateway {
    class CommandQueue;
    void register_rpc_handlers(
        GatewayServer& server,
        std::shared_ptr<quantclaw::SessionManager> session_manager,
        std::shared_ptr<quantclaw::AgentLoop> agent_loop,
        std::shared_ptr<quantclaw::PromptBuilder> prompt_builder,
        std::shared_ptr<quantclaw::ToolRegistry> tool_registry,
        const quantclaw::QuantClawConfig& config,
        std::shared_ptr<spdlog::logger> logger,
        std::function<void()> reload_fn = nullptr,
        std::shared_ptr<quantclaw::ProviderRegistry> provider_registry = nullptr,
        std::shared_ptr<quantclaw::SkillLoader> skill_loader = nullptr,
        std::shared_ptr<quantclaw::CronScheduler> cron_scheduler = nullptr,
        std::shared_ptr<quantclaw::ExecApprovalManager> exec_approval_mgr = nullptr,
        PluginSystem* plugin_system = nullptr,
        quantclaw::gateway::CommandQueue* command_queue = nullptr,
        std::string log_file_path = {});
}

// --- Mock LLM Provider for Load Testing ---

class LoadTestMockProvider : public quantclaw::LLMProvider {
public:
    std::atomic<int> request_count{0};
    int response_delay_ms = 10;  // 模拟响应延迟

    quantclaw::ChatCompletionResponse ChatCompletion(
        const quantclaw::ChatCompletionRequest& /*request*/) override {
        request_count++;
        if (response_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(response_delay_ms));
        }
        quantclaw::ChatCompletionResponse resp;
        resp.content = "Load test response";
        resp.finish_reason = "stop";
        return resp;
    }

    void ChatCompletionStream(
        const quantclaw::ChatCompletionRequest& /*request*/,
        std::function<void(const quantclaw::ChatCompletionResponse&)> callback) override {
        request_count++;
        if (response_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(response_delay_ms));
        }
        quantclaw::ChatCompletionResponse resp;
        resp.content = "Load test response";
        resp.is_stream_end = true;
        resp.finish_reason = "stop";
        callback(resp);
    }

    std::string GetProviderName() const override { return "load-test-mock"; }
    std::vector<std::string> GetSupportedModels() const override { return {"mock"}; }
};

// --- Load Test Fixture ---

class LoadTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = quantclaw::test::MakeTestDir("quantclaw_load_test");
        workspace_dir_ = test_dir_ / "workspace";
        sessions_dir_ = test_dir_ / "sessions";
        std::filesystem::create_directories(workspace_dir_);
        std::filesystem::create_directories(sessions_dir_);

        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("load_test", null_sink);

        config_.agent.model = "mock";
        config_.agent.max_iterations = 3;
        config_.gateway.port = port_;
        config_.gateway.auth.mode = "none";

        memory_manager_ = std::make_shared<quantclaw::MemoryManager>(workspace_dir_, logger_);
        skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
        tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
        tool_registry_->RegisterBuiltinTools();

        mock_provider_ = std::make_shared<LoadTestMockProvider>();
        agent_loop_ = std::make_shared<quantclaw::AgentLoop>(
            memory_manager_, skill_loader_, tool_registry_, mock_provider_, config_.agent, logger_);

        session_manager_ = std::make_shared<quantclaw::SessionManager>(sessions_dir_, logger_);
        prompt_builder_ = std::make_shared<quantclaw::PromptBuilder>(
            memory_manager_, skill_loader_, tool_registry_);

        server_ = std::make_unique<quantclaw::gateway::GatewayServer>(port_, logger_);
        server_->SetAuth(config_.gateway.auth.mode, config_.gateway.auth.token);

        quantclaw::gateway::register_rpc_handlers(
            *server_, session_manager_, agent_loop_, prompt_builder_,
            tool_registry_, config_, logger_);

        server_->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void TearDown() override {
        if (server_) {
            server_->Stop();
            server_.reset();
        }
        if (std::filesystem::exists(test_dir_)) {
            std::filesystem::remove_all(test_dir_);
        }
    }

    int port_ = quantclaw::test::FindFreePort();
    std::filesystem::path test_dir_;
    std::filesystem::path workspace_dir_;
    std::filesystem::path sessions_dir_;
    std::shared_ptr<spdlog::logger> logger_;
    quantclaw::QuantClawConfig config_;
    std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
    std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
    std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
    std::shared_ptr<LoadTestMockProvider> mock_provider_;
    std::shared_ptr<quantclaw::AgentLoop> agent_loop_;
    std::shared_ptr<quantclaw::SessionManager> session_manager_;
    std::shared_ptr<quantclaw::PromptBuilder> prompt_builder_;
    std::unique_ptr<quantclaw::gateway::GatewayServer> server_;
};

// ======================================================================
// Performance Tests
// ======================================================================

// Requirements: Performance Requirements - 测试并发连接
TEST_F(LoadTest, ConcurrentConnections_10Clients) {
    const int num_clients = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_clients; ++i) {
        threads.emplace_back([this, &success_count, &failure_count, i]() {
            try {
                std::string url = "ws://127.0.0.1:" + std::to_string(port_);
                auto client = std::make_unique<quantclaw::gateway::GatewayClient>(
                    url, "", logger_);

                if (client->Connect(5000)) {
                    auto result = client->Call("gateway.health");
                    if (result["status"] == "ok") {
                        success_count++;
                    } else {
                        failure_count++;
                    }
                    client->Disconnect();
                } else {
                    failure_count++;
                }
            } catch (...) {
                failure_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(success_count.load(), num_clients);
    EXPECT_EQ(failure_count.load(), 0);

    // 性能指标: 10 个并发连接应在 5 秒内完成
    EXPECT_LT(duration.count(), 5000);

    std::cout << "  10 concurrent connections completed in "
              << duration.count() << "ms" << std::endl;
}

// Requirements: Performance Requirements - 测试高频消息处理
TEST_F(LoadTest, HighFrequencyMessages_10MessagesPerSecond) {
    std::string url = "ws://127.0.0.1:" + std::to_string(port_);
    auto client = std::make_unique<quantclaw::gateway::GatewayClient>(url, "", logger_);
    ASSERT_TRUE(client->Connect(5000));

    const int num_messages = 20;
    std::vector<int64_t> response_times;

    for (int i = 0; i < num_messages; ++i) {
        auto start = std::chrono::steady_clock::now();

        auto result = client->Call("gateway.health");

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        response_times.push_back(duration.count());

        EXPECT_EQ(result["status"], "ok");

        // 模拟 10 messages/second = 100ms 间隔
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    client->Disconnect();

    // 计算平均响应时间
    int64_t total_time = 0;
    for (auto t : response_times) {
        total_time += t;
    }
    int64_t avg_time = total_time / response_times.size();

    // 性能指标: 平均响应时间应 < 100ms
    EXPECT_LT(avg_time, 100);

    std::cout << "  Average response time: " << avg_time << "ms" << std::endl;
}

// Requirements: Performance Requirements - 测试简单请求响应时间
TEST_F(LoadTest, SimpleRequestResponseTime) {
    std::string url = "ws://127.0.0.1:" + std::to_string(port_);
    auto client = std::make_unique<quantclaw::gateway::GatewayClient>(url, "", logger_);
    ASSERT_TRUE(client->Connect(5000));

    // 预热
    for (int i = 0; i < 5; ++i) {
        client->Call("gateway.health");
    }

    // 测量
    const int num_requests = 50;
    std::vector<int64_t> response_times;

    for (int i = 0; i < num_requests; ++i) {
        auto start = std::chrono::steady_clock::now();
        auto result = client->Call("gateway.health");
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        response_times.push_back(duration.count());

        EXPECT_EQ(result["status"], "ok");
    }

    client->Disconnect();

    // 统计
    int64_t total = 0;
    int64_t min_time = response_times[0];
    int64_t max_time = response_times[0];

    for (auto t : response_times) {
        total += t;
        min_time = std::min(min_time, t);
        max_time = std::max(max_time, t);
    }

    int64_t avg_time = total / response_times.size();

    // 性能指标: 简单请求平均响应时间应 < 50ms
    EXPECT_LT(avg_time, 50);

    std::cout << "  Simple request stats:" << std::endl;
    std::cout << "    Average: " << avg_time << "ms" << std::endl;
    std::cout << "    Min: " << min_time << "ms" << std::endl;
    std::cout << "    Max: " << max_time << "ms" << std::endl;
}

// Requirements: Performance Requirements - 测试复杂请求响应时间
TEST_F(LoadTest, ComplexRequestResponseTime) {
    mock_provider_->response_delay_ms = 100;  // 模拟 LLM 延迟

    std::string url = "ws://127.0.0.1:" + std::to_string(port_);
    auto client = std::make_unique<quantclaw::gateway::GatewayClient>(url, "", logger_);
    ASSERT_TRUE(client->Connect(5000));

    const int num_requests = 10;
    std::vector<int64_t> response_times;

    for (int i = 0; i < num_requests; ++i) {
        nlohmann::json params = {
            {"message", "Test message " + std::to_string(i)},
            {"session_id", "load-test-session"}
        };

        auto start = std::chrono::steady_clock::now();
        auto result = client->Call("agent.request", params);
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        response_times.push_back(duration.count());
    }

    client->Disconnect();

    // 统计
    int64_t total = 0;
    for (auto t : response_times) {
        total += t;
    }
    int64_t avg_time = total / response_times.size();

    // 性能指标: 复杂请求平均响应时间应 < 2000ms (包含 LLM 延迟)
    EXPECT_LT(avg_time, 2000);

    std::cout << "  Complex request average time: " << avg_time << "ms" << std::endl;
}

// Requirements: Performance Requirements - 测试吞吐量
TEST_F(LoadTest, Throughput_RequestsPerSecond) {
    std::string url = "ws://127.0.0.1:" + std::to_string(port_);
    auto client = std::make_unique<quantclaw::gateway::GatewayClient>(url, "", logger_);
    ASSERT_TRUE(client->Connect(5000));

    const int duration_seconds = 5;
    std::atomic<int> request_count{0};
    std::atomic<bool> stop_flag{false};

    auto start = std::chrono::steady_clock::now();

    std::thread worker([&]() {
        while (!stop_flag.load()) {
            try {
                auto result = client->Call("gateway.health");
                if (result["status"] == "ok") {
                    request_count++;
                }
            } catch (...) {
                // 忽略错误,继续测试
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
    stop_flag = true;
    worker.join();

    auto end = std::chrono::steady_clock::now();
    auto actual_duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    client->Disconnect();

    double requests_per_second = static_cast<double>(request_count.load()) / actual_duration.count();

    // 性能指标: 应能处理 > 50 requests/second
    EXPECT_GT(requests_per_second, 50.0);

    std::cout << "  Throughput: " << requests_per_second << " requests/second" << std::endl;
    std::cout << "  Total requests: " << request_count.load() << std::endl;
}
