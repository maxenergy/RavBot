// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "ravbot/gateway/gateway_server.hpp"
#include "ravbot/gateway/gateway_client.hpp"
#include "ravbot/core/agent_loop.hpp"
#include "ravbot/core/memory_manager.hpp"
#include "ravbot/session/session_manager.hpp"
#include "ravbot/tools/tool_registry.hpp"
#include "ravbot/providers/llm_provider.hpp"
#include "ravbot/config.hpp"
#include "ravbot/core/skill_loader.hpp"
#include "ravbot/core/prompt_builder.hpp"
#include "ravbot/plugins/plugin_system.hpp"
#include "../test_helpers.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

namespace ravbot::gateway {
    class CommandQueue;
    void register_rpc_handlers(
        GatewayServer& server,
        std::shared_ptr<ravbot::SessionManager> session_manager,
        std::shared_ptr<ravbot::AgentLoop> agent_loop,
        std::shared_ptr<ravbot::PromptBuilder> prompt_builder,
        std::shared_ptr<ravbot::ToolRegistry> tool_registry,
        const ravbot::RavBotConfig& config,
        std::shared_ptr<spdlog::logger> logger,
        std::function<void()> reload_fn = nullptr,
        std::shared_ptr<ravbot::ProviderRegistry> provider_registry = nullptr,
        std::shared_ptr<ravbot::SkillLoader> skill_loader = nullptr,
        std::shared_ptr<ravbot::CronScheduler> cron_scheduler = nullptr,
        std::shared_ptr<ravbot::ExecApprovalManager> exec_approval_mgr = nullptr,
        PluginSystem* plugin_system = nullptr,
        ravbot::gateway::CommandQueue* command_queue = nullptr,
        std::string log_file_path = {});
}

// --- Mock LLM Provider for Memory Testing ---

class MemoryTestMockProvider : public ravbot::LLMProvider {
public:
    std::atomic<int> request_count{0};
    std::string response_content = "Memory test response";

    ravbot::ChatCompletionResponse ChatCompletion(
        const ravbot::ChatCompletionRequest& /*request*/) override {
        request_count++;
        ravbot::ChatCompletionResponse resp;
        resp.content = response_content;
        resp.finish_reason = "stop";
        return resp;
    }

    void ChatCompletionStream(
        const ravbot::ChatCompletionRequest& /*request*/,
        std::function<void(const ravbot::ChatCompletionResponse&)> callback) override {
        request_count++;
        ravbot::ChatCompletionResponse resp;
        resp.content = response_content;
        resp.is_stream_end = true;
        resp.finish_reason = "stop";
        callback(resp);
    }

    std::string GetProviderName() const override { return "memory-test-mock"; }
    std::vector<std::string> GetSupportedModels() const override { return {"mock"}; }
};

// --- Memory Test Fixture ---

class MemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = ravbot::test::MakeTestDir("ravbot_memory_test");
        workspace_dir_ = test_dir_ / "workspace";
        sessions_dir_ = test_dir_ / "sessions";
        std::filesystem::create_directories(workspace_dir_);
        std::filesystem::create_directories(sessions_dir_);

        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("memory_test", null_sink);

        config_.agent.model = "mock";
        config_.agent.max_iterations = 3;
        config_.gateway.port = port_;
        config_.gateway.auth.mode = "none";

        memory_manager_ = std::make_shared<ravbot::MemoryManager>(workspace_dir_, logger_);
        skill_loader_ = std::make_shared<ravbot::SkillLoader>(logger_);
        tool_registry_ = std::make_shared<ravbot::ToolRegistry>(logger_);
        tool_registry_->RegisterBuiltinTools();

        mock_provider_ = std::make_shared<MemoryTestMockProvider>();
        agent_loop_ = std::make_shared<ravbot::AgentLoop>(
            memory_manager_, skill_loader_, tool_registry_, mock_provider_, config_.agent, logger_);

        session_manager_ = std::make_shared<ravbot::SessionManager>(sessions_dir_, logger_);
        prompt_builder_ = std::make_shared<ravbot::PromptBuilder>(
            memory_manager_, skill_loader_, tool_registry_);

        server_ = std::make_unique<ravbot::gateway::GatewayServer>(port_, logger_);
        server_->SetAuth(config_.gateway.auth.mode, config_.gateway.auth.token);

        ravbot::gateway::register_rpc_handlers(
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

    // 获取当前进程内存使用量 (Linux /proc/self/status)
    size_t GetMemoryUsage() {
        std::ifstream status("/proc/self/status");
        if (!status.is_open()) {
            return 0;
        }

        std::string line;
        while (std::getline(status, line)) {
            if (line.find("VmRSS:") == 0) {
                // VmRSS: 12345 kB
                size_t pos = line.find_last_of(' ');
                if (pos != std::string::npos) {
                    std::string kb_str = line.substr(line.find_last_of(' ', pos - 1) + 1, pos - line.find_last_of(' ', pos - 1) - 1);
                    return std::stoull(kb_str) * 1024;  // 转换为字节
                }
            }
        }
        return 0;
    }

    int port_ = ravbot::test::FindFreePort();
    std::filesystem::path test_dir_;
    std::filesystem::path workspace_dir_;
    std::filesystem::path sessions_dir_;
    std::shared_ptr<spdlog::logger> logger_;
    ravbot::RavBotConfig config_;
    std::shared_ptr<ravbot::MemoryManager> memory_manager_;
    std::shared_ptr<ravbot::SkillLoader> skill_loader_;
    std::shared_ptr<ravbot::ToolRegistry> tool_registry_;
    std::shared_ptr<MemoryTestMockProvider> mock_provider_;
    std::shared_ptr<ravbot::AgentLoop> agent_loop_;
    std::shared_ptr<ravbot::SessionManager> session_manager_;
    std::shared_ptr<ravbot::PromptBuilder> prompt_builder_;
    std::unique_ptr<ravbot::gateway::GatewayServer> server_;
};

// ======================================================================
// Memory Tests
// ======================================================================

// Requirements: Performance Requirements - 测试长时间运行内存使用
TEST_F(MemoryTest, LongRunningMemoryUsage) {
    std::string url = "ws://127.0.0.1:" + std::to_string(port_);
    auto client = std::make_unique<ravbot::gateway::GatewayClient>(url, "", logger_);
    ASSERT_TRUE(client->Connect(5000));

    // 记录初始内存
    size_t initial_memory = GetMemoryUsage();
    ASSERT_GT(initial_memory, 0u) << "Failed to get memory usage";

    // 发送 100 个请求
    const int num_requests = 100;
    for (int i = 0; i < num_requests; ++i) {
        nlohmann::json params = {
            {"message", "Test message " + std::to_string(i)},
            {"session_id", "memory-test-session"}
        };
        auto result = client->Call("agent.request", params);
        EXPECT_TRUE(result.contains("response"));
    }

    // 记录最终内存
    size_t final_memory = GetMemoryUsage();
    ASSERT_GT(final_memory, 0u) << "Failed to get memory usage";

    client->Disconnect();

    // 计算内存增长
    int64_t memory_growth = static_cast<int64_t>(final_memory) - static_cast<int64_t>(initial_memory);
    double growth_mb = memory_growth / (1024.0 * 1024.0);

    std::cout << "  Initial memory: " << (initial_memory / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  Final memory: " << (final_memory / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  Memory growth: " << growth_mb << " MB" << std::endl;

    // 性能指标: 100 个请求的内存增长应 < 50 MB
    EXPECT_LT(growth_mb, 50.0) << "Memory growth exceeds 50 MB";
}

// Requirements: Performance Requirements - 测试大 transcript 内存占用
TEST_F(MemoryTest, LargeTranscriptMemoryUsage) {
    std::string url = "ws://127.0.0.1:" + std::to_string(port_);
    auto client = std::make_unique<ravbot::gateway::GatewayClient>(url, "", logger_);
    ASSERT_TRUE(client->Connect(5000));

    // 创建大消息 (10 KB)
    std::string large_message(10 * 1024, 'x');
    mock_provider_->response_content = large_message;

    // 记录初始内存
    size_t initial_memory = GetMemoryUsage();
    ASSERT_GT(initial_memory, 0u);

    // 发送 50 个大消息请求
    const int num_requests = 50;
    for (int i = 0; i < num_requests; ++i) {
        nlohmann::json params = {
            {"message", large_message},
            {"session_id", "large-transcript-session"}
        };
        auto result = client->Call("agent.request", params);
        EXPECT_TRUE(result.contains("response"));
    }

    // 记录最终内存
    size_t final_memory = GetMemoryUsage();
    ASSERT_GT(final_memory, 0u);

    client->Disconnect();

    // 计算内存增长
    int64_t memory_growth = static_cast<int64_t>(final_memory) - static_cast<int64_t>(initial_memory);
    double growth_mb = memory_growth / (1024.0 * 1024.0);

    std::cout << "  Initial memory: " << (initial_memory / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  Final memory: " << (final_memory / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  Memory growth: " << growth_mb << " MB" << std::endl;
    std::cout << "  Total data sent: " << (num_requests * large_message.size() / 1024 / 1024) << " MB" << std::endl;

    // 性能指标: 50 个 10KB 消息的内存增长应 < 100 MB
    EXPECT_LT(growth_mb, 100.0) << "Memory growth exceeds 100 MB for large transcripts";
}

// Requirements: Performance Requirements - 测试内存泄漏
TEST_F(MemoryTest, MemoryLeakDetection) {
    std::string url = "ws://127.0.0.1:" + std::to_string(port_);

    // 多次连接和断开,检测内存泄漏
    const int num_iterations = 10;
    std::vector<size_t> memory_samples;

    for (int i = 0; i < num_iterations; ++i) {
        auto client = std::make_unique<ravbot::gateway::GatewayClient>(url, "", logger_);
        ASSERT_TRUE(client->Connect(5000));

        // 发送 10 个请求
        for (int j = 0; j < 10; ++j) {
            nlohmann::json params = {
                {"message", "Leak test message"},
                {"session_id", "leak-test-session-" + std::to_string(i)}
            };
            auto result = client->Call("agent.request", params);
            EXPECT_TRUE(result.contains("response"));
        }

        client->Disconnect();
        client.reset();

        // 记录内存使用
        size_t current_memory = GetMemoryUsage();
        ASSERT_GT(current_memory, 0u);
        memory_samples.push_back(current_memory);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 分析内存趋势
    size_t first_memory = memory_samples[0];
    size_t last_memory = memory_samples[num_iterations - 1];
    int64_t total_growth = static_cast<int64_t>(last_memory) - static_cast<int64_t>(first_memory);
    double growth_mb = total_growth / (1024.0 * 1024.0);

    std::cout << "  Memory samples (MB): ";
    for (size_t mem : memory_samples) {
        std::cout << (mem / 1024 / 1024) << " ";
    }
    std::cout << std::endl;
    std::cout << "  Total memory growth: " << growth_mb << " MB" << std::endl;

    // 性能指标: 10 次迭代的内存增长应 < 20 MB (允许一些缓存和缓冲区)
    EXPECT_LT(growth_mb, 20.0) << "Potential memory leak detected";
}

// Requirements: Performance Requirements - 测试 Session 内存管理
TEST_F(MemoryTest, SessionMemoryManagement) {
    std::string url = "ws://127.0.0.1:" + std::to_string(port_);
    auto client = std::make_unique<ravbot::gateway::GatewayClient>(url, "", logger_);
    ASSERT_TRUE(client->Connect(5000));

    // 记录初始内存
    size_t initial_memory = GetMemoryUsage();
    ASSERT_GT(initial_memory, 0u);

    // 创建 50 个不同的 session
    const int num_sessions = 50;
    for (int i = 0; i < num_sessions; ++i) {
        nlohmann::json params = {
            {"message", "Session test message"},
            {"session_id", "session-" + std::to_string(i)}
        };
        auto result = client->Call("agent.request", params);
        EXPECT_TRUE(result.contains("response"));
    }

    // 记录创建 session 后的内存
    size_t after_create_memory = GetMemoryUsage();
    ASSERT_GT(after_create_memory, 0u);

    client->Disconnect();

    // 计算内存变化
    int64_t create_growth = static_cast<int64_t>(after_create_memory) - static_cast<int64_t>(initial_memory);
    double create_mb = create_growth / (1024.0 * 1024.0);

    std::cout << "  Initial memory: " << (initial_memory / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  After create: " << (after_create_memory / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  Memory growth: " << create_mb << " MB" << std::endl;

    // 性能指标: 50 个 session 的内存增长应 < 30 MB
    EXPECT_LT(create_mb, 30.0) << "Memory growth for 50 sessions exceeds 30 MB";
}
