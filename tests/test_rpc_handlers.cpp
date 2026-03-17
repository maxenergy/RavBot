// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "ravbot/config.hpp"
#include "ravbot/core/agent_loop.hpp"
#include "ravbot/core/memory_manager.hpp"
#include "ravbot/core/prompt_builder.hpp"
#include "ravbot/core/skill_loader.hpp"
#include "ravbot/gateway/gateway_client.hpp"
#include "ravbot/gateway/gateway_server.hpp"
#include "ravbot/gateway/protocol.hpp"
#include "ravbot/providers/llm_provider.hpp"
#include "ravbot/session/session_manager.hpp"
#include "ravbot/tools/tool_chain.hpp"
#include "ravbot/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

// Forward declare
namespace ravbot {
class ProviderRegistry;
class SkillLoader;
class CronScheduler;
class ExecApprovalManager;
class PluginSystem;
}  // namespace ravbot
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
    ravbot::PluginSystem* plugin_system = nullptr,
    ravbot::gateway::CommandQueue* command_queue = nullptr,
    std::string log_file_path = {});
}  // namespace ravbot::gateway

// Minimal mock LLM
class RpcMockLLMProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest&) override {
    ravbot::ChatCompletionResponse resp;
    resp.content = "mock reply";
    resp.finish_reason = "stop";
    return resp;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest&,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    ravbot::ChatCompletionResponse delta;
    delta.content = "mock reply";
    callback(delta);

    ravbot::ChatCompletionResponse end;
    end.content = "mock reply";
    end.is_stream_end = true;
    callback(end);
  }

  std::string GetProviderName() const override {
    return "rpc-mock";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock"};
  }
};

class RpcHandlersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = ravbot::test::MakeTestDir("ravbot_rpc_test");
    workspace_dir_ = test_dir_ / "workspace";
    sessions_dir_ = test_dir_ / "sessions";
    std::filesystem::create_directories(workspace_dir_);
    std::filesystem::create_directories(sessions_dir_);

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("rpc_test", null_sink);

    config_.agent.model = "mock-model";
    config_.agent.max_iterations = 3;
    config_.agent.temperature = 0.0;
    config_.agent.max_tokens = 512;
    config_.gateway.port = port_;
    config_.gateway.auth.mode = "none";

    memory_manager_ =
        std::make_shared<ravbot::MemoryManager>(workspace_dir_, logger_);
    skill_loader_ = std::make_shared<ravbot::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<ravbot::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();
    tool_registry_->RegisterChainTool();

    mock_llm_ = std::make_shared<RpcMockLLMProvider>();
    agent_loop_ = std::make_shared<ravbot::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_llm_,
        config_.agent, logger_);
    session_manager_ =
        std::make_shared<ravbot::SessionManager>(sessions_dir_, logger_);
    prompt_builder_ = std::make_shared<ravbot::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);

    server_ =
        std::make_unique<ravbot::gateway::GatewayServer>(port_, logger_);
    server_->SetAuth("none", "");

    ravbot::gateway::register_rpc_handlers(
        *server_, session_manager_, agent_loop_, prompt_builder_,
        tool_registry_, config_, logger_, nullptr, nullptr, skill_loader_);

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

  std::unique_ptr<ravbot::gateway::GatewayClient> make_client() {
    std::string url = "ws://127.0.0.1:" + std::to_string(port_);
    return std::make_unique<ravbot::gateway::GatewayClient>(url, "",
                                                               logger_);
  }

  static int next_port() {
    return ravbot::test::FindFreePort();
  }

  int port_ = next_port();
  std::filesystem::path test_dir_;
  std::filesystem::path workspace_dir_;
  std::filesystem::path sessions_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  ravbot::RavBotConfig config_;
  std::shared_ptr<ravbot::MemoryManager> memory_manager_;
  std::shared_ptr<ravbot::SkillLoader> skill_loader_;
  std::shared_ptr<ravbot::ToolRegistry> tool_registry_;
  std::shared_ptr<RpcMockLLMProvider> mock_llm_;
  std::shared_ptr<ravbot::AgentLoop> agent_loop_;
  std::shared_ptr<ravbot::SessionManager> session_manager_;
  std::shared_ptr<ravbot::PromptBuilder> prompt_builder_;
  std::unique_ptr<ravbot::gateway::GatewayServer> server_;
};

// --- config.get edge cases ---

TEST_F(RpcHandlersTest, ConfigGetUnknownPath) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  // Unknown config path should error
  EXPECT_THROW(client->Call("config.get", {{"path", "nonexistent.key"}}, 5000),
               std::runtime_error);

  client->Disconnect();
}

TEST_F(RpcHandlersTest, ConfigGetGatewayPort) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("config.get", {{"path", "gateway.port"}});
  EXPECT_EQ(result.get<int>(), port_);

  client->Disconnect();
}

TEST_F(RpcHandlersTest, ConfigGetGatewayBind) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("config.get", {{"path", "gateway.bind"}});
  EXPECT_FALSE(result.get<std::string>().empty());

  client->Disconnect();
}

TEST_F(RpcHandlersTest, ConfigGetAgentTemperature) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("config.get", {{"path", "agent.temperature"}});
  EXPECT_DOUBLE_EQ(result.get<double>(), 0.0);

  client->Disconnect();
}

TEST_F(RpcHandlersTest, ConfigGetAgentMaxIterations) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("config.get", {{"path", "agent.maxIterations"}});
  EXPECT_EQ(result.get<int>(), 3);

  client->Disconnect();
}

// --- agent.request edge cases ---

TEST_F(RpcHandlersTest, AgentRequestEmptyMessage) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  EXPECT_THROW(client->Call("agent.request", {{"message", ""}}, 5000),
               std::runtime_error);

  client->Disconnect();
}

TEST_F(RpcHandlersTest, AgentRequestCustomSessionKey) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call(
      "agent.request",
      {{"message", "Hello"}, {"sessionKey", "custom:session:key"}}, 10000);

  EXPECT_EQ(result["sessionKey"], "custom:session:key");

  client->Disconnect();
}

TEST_F(RpcHandlersTest, SkillsStatusReturnsRichSkillPayload) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  const char* old_home = std::getenv("HOME");
  std::string old_home_str = old_home ? old_home : "";
  struct HomeRestoreGuard {
    const char* old_home;
    std::string old_home_str;
    ~HomeRestoreGuard() {
      if (old_home) {
        setenv("HOME", old_home_str.c_str(), 1);
      } else {
        unsetenv("HOME");
      }
    }
  };
  HomeRestoreGuard home_guard{old_home, old_home_str};

  auto home_dir = test_dir_ / "home";
  auto skill_dir = home_dir / ".ravbot" / "skills" / "sample-skill";
  std::filesystem::create_directories(skill_dir);

  std::ofstream skill_file(skill_dir / "SKILL.md");
  skill_file << "---\n"
             << "name: Sample Skill\n"
             << "description: Sample skill description\n"
             << "skillKey: sample-skill\n"
             << "always: true\n"
             << "install:\n"
             << "  - kind: uv\n"
             << "    package: sample-package\n"
             << "    bins:\n"
             << "      - sample-bin\n"
             << "---\n"
             << "Sample skill body.\n";
  skill_file.close();

  setenv("HOME", home_dir.string().c_str(), 1);
  auto result = client->Call("skills.status", nlohmann::json::object());

  ASSERT_TRUE(result.contains("skills"));
  ASSERT_FALSE(result["skills"].empty());

  const auto& skill = result["skills"][0];
  EXPECT_EQ(skill["skillKey"], "sample-skill");
  ASSERT_TRUE(skill.contains("install"));
  ASSERT_TRUE(skill["install"].is_array());
  ASSERT_TRUE(skill.contains("missing"));
  ASSERT_TRUE(skill["missing"]["bins"].is_array());

  client->Disconnect();
}

TEST_F(RpcHandlersTest, SecretsResolveRejectsLegacyKeyShape) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  EXPECT_THROW(
      client->Call("secrets.resolve", {{"key", "OPENAI_API_KEY"}}, 5000),
      std::runtime_error);

  client->Disconnect();
}

TEST_F(RpcHandlersTest, SecretsResolveDoesNotExposeEnvironmentValues) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  setenv("OPENAI_API_KEY", "sk-top-secret", 1);
  try {
    client->Call("secrets.resolve",
                 {{"commandName", "message"},
                  {"targetIds", nlohmann::json::array({"talk.apiKey"})}},
                 5000);
    FAIL() << "expected secrets.resolve to fail until gateway-side resolution "
              "is implemented";
  } catch (const std::runtime_error& err) {
    std::string message = err.what();
    EXPECT_NE(message.find("secrets.resolve"), std::string::npos);
    EXPECT_EQ(message.find("sk-top-secret"), std::string::npos);
  }

  client->Disconnect();
}

// --- sessions.history edge cases ---

TEST_F(RpcHandlersTest, SessionsHistoryMissingKey) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  EXPECT_THROW(client->Call("sessions.history", nlohmann::json::object(), 5000),
               std::runtime_error);

  client->Disconnect();
}

// --- sessions.list pagination ---

TEST_F(RpcHandlersTest, SessionsListEmptyInitially) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("sessions.list", nlohmann::json::object());
  // New shape: {ts, path, count, defaults, sessions:[]}
  ASSERT_TRUE(result.is_object());
  ASSERT_TRUE(result.contains("sessions"));
  ASSERT_TRUE(result["sessions"].is_array());
  EXPECT_EQ(result["sessions"].size(), 0u);
  EXPECT_TRUE(result.contains("count"));
  EXPECT_TRUE(result.contains("defaults"));

  client->Disconnect();
}

TEST_F(RpcHandlersTest, SessionsListWithLimitOffset) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  // Create two sessions
  client->Call("agent.request", {{"message", "Hi"}, {"sessionKey", "a:1:main"}},
               10000);
  client->Call("agent.request",
               {{"message", "Hello"}, {"sessionKey", "b:2:main"}}, 10000);

  // List all — new shape returns {sessions:[], count:N, ...}
  auto all = client->Call("sessions.list", {{"limit", 50}, {"offset", 0}});
  ASSERT_TRUE(all.is_object());
  ASSERT_TRUE(all.contains("sessions"));
  auto& all_sessions = all["sessions"];
  ASSERT_TRUE(all_sessions.is_array());
  EXPECT_GE(all_sessions.size(), 2u);

  // List with limit=1
  auto limited = client->Call("sessions.list", {{"limit", 1}, {"offset", 0}});
  ASSERT_TRUE(limited.is_object());
  EXPECT_EQ(limited["sessions"].size(), 1u);

  // List with offset past all sessions
  auto empty = client->Call("sessions.list", {{"limit", 10}, {"offset", 100}});
  ASSERT_TRUE(empty.is_object());
  EXPECT_EQ(empty["sessions"].size(), 0u);

  client->Disconnect();
}

// --- sessions.list returns valid timestamps ---

TEST_F(RpcHandlersTest, SessionsListUpdatedAtIsTimestamp) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  // Create a session
  client->Call("agent.request",
               {{"message", "Hi"}, {"sessionKey", "test:main"}}, 10000);

  // List sessions
  auto result = client->Call("sessions.list", nlohmann::json::object());
  ASSERT_TRUE(result.is_object());
  ASSERT_TRUE(result.contains("sessions"));
  ASSERT_TRUE(result["sessions"].is_array());
  EXPECT_GT(result["sessions"].size(), 0u);

  // Check that updatedAt is a non-zero integer (milliseconds since epoch)
  const auto& session = result["sessions"][0];
  ASSERT_TRUE(session.contains("updatedAt"));
  ASSERT_TRUE(session["updatedAt"].is_number_integer());
  int64_t updated_at_ms = session["updatedAt"].get<int64_t>();
  EXPECT_GT(updated_at_ms, 0);

  // Verify it's a reasonable timestamp (within last hour)
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  EXPECT_LT(now_ms - updated_at_ms, 3600000);  // Less than 1 hour ago

  client->Disconnect();
}

// --- health returns uptime and version ---

TEST_F(RpcHandlersTest, HealthContainsUptime) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("gateway.health");
  EXPECT_TRUE(result.contains("uptime"));
  EXPECT_GE(result["uptime"].get<int>(), 0);

  client->Disconnect();
}

// --- status shows port ---

TEST_F(RpcHandlersTest, StatusShowsPort) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("gateway.status");
  EXPECT_EQ(result["port"].get<int>(), port_);

  client->Disconnect();
}

// --- config.reload RPC test ---

class RpcReloadTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = ravbot::test::MakeTestDir("ravbot_rpc_reload_test");
    workspace_dir_ = test_dir_ / "workspace";
    sessions_dir_ = test_dir_ / "sessions";
    std::filesystem::create_directories(workspace_dir_);
    std::filesystem::create_directories(sessions_dir_);

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("rpc_reload_test", null_sink);

    config_.agent.model = "mock-model";
    config_.agent.max_iterations = 3;
    config_.agent.temperature = 0.0;
    config_.agent.max_tokens = 512;
    config_.gateway.port = port_;
    config_.gateway.auth.mode = "none";

    memory_manager_ =
        std::make_shared<ravbot::MemoryManager>(workspace_dir_, logger_);
    skill_loader_ = std::make_shared<ravbot::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<ravbot::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();
    tool_registry_->RegisterChainTool();

    mock_llm_ = std::make_shared<RpcMockLLMProvider>();
    agent_loop_ = std::make_shared<ravbot::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_llm_,
        config_.agent, logger_);
    session_manager_ =
        std::make_shared<ravbot::SessionManager>(sessions_dir_, logger_);
    prompt_builder_ = std::make_shared<ravbot::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);

    reload_called_ = false;
    reload_fn_ = [this]() { reload_called_ = true; };

    server_ =
        std::make_unique<ravbot::gateway::GatewayServer>(port_, logger_);
    server_->SetAuth("none", "");

    ravbot::gateway::register_rpc_handlers(
        *server_, session_manager_, agent_loop_, prompt_builder_,
        tool_registry_, config_, logger_, reload_fn_);

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

  std::unique_ptr<ravbot::gateway::GatewayClient> make_client() {
    std::string url = "ws://127.0.0.1:" + std::to_string(port_);
    return std::make_unique<ravbot::gateway::GatewayClient>(url, "",
                                                               logger_);
  }

  static int next_port() {
    return ravbot::test::FindFreePort();
  }

  int port_ = next_port();
  std::filesystem::path test_dir_;
  std::filesystem::path workspace_dir_;
  std::filesystem::path sessions_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  ravbot::RavBotConfig config_;
  std::shared_ptr<ravbot::MemoryManager> memory_manager_;
  std::shared_ptr<ravbot::SkillLoader> skill_loader_;
  std::shared_ptr<ravbot::ToolRegistry> tool_registry_;
  std::shared_ptr<RpcMockLLMProvider> mock_llm_;
  std::shared_ptr<ravbot::AgentLoop> agent_loop_;
  std::shared_ptr<ravbot::SessionManager> session_manager_;
  std::shared_ptr<ravbot::PromptBuilder> prompt_builder_;
  std::unique_ptr<ravbot::gateway::GatewayServer> server_;
  std::function<void()> reload_fn_;
  bool reload_called_;
};

TEST_F(RpcReloadTest, ConfigReloadRPC) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  EXPECT_FALSE(reload_called_);

  auto result = client->Call("config.reload", {});
  EXPECT_EQ(result["ok"], true);
  EXPECT_TRUE(reload_called_);

  client->Disconnect();
}

// ================================================================
// OpenClaw protocol compatibility tests
// ================================================================

// Test that OpenClaw-style "connect" method with nested params returns
// capabilities
TEST_F(RpcHandlersTest, OpenClawConnect) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  // Send OpenClaw-style connect with nested params
  auto result = client->Call(
      "connect",
      {{"client", {{"name", "openclaw-test"}, {"version", "1.0.0"}}},
       {"auth", {{"token", ""}}},
       {"device", {{"id", "test-device"}}}},
      5000);

  // Verify OpenClaw hello-ok response contains capabilities
  EXPECT_TRUE(result.contains("capabilities"));
  EXPECT_TRUE(result["capabilities"].is_array());
  EXPECT_TRUE(result.contains("authenticated"));
  EXPECT_EQ(result["authenticated"], true);
  EXPECT_TRUE(result.contains("protocol"));

  client->Disconnect();
}

// Test chat.send streaming with OpenClaw event format
TEST_F(RpcHandlersTest, ChatSendStreaming) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  // Subscribe to OpenClaw events
  std::atomic<int> agent_events{0};
  std::atomic<int> chat_events{0};
  client->Subscribe("agent", [&agent_events](const std::string&,
                                             const nlohmann::json& payload) {
    agent_events++;
  });
  client->Subscribe("chat", [&chat_events](const std::string&,
                                           const nlohmann::json& payload) {
    // Verify chat event has "state":"final"
    if (payload.contains("state") && payload["state"] == "final") {
      chat_events++;
    }
  });

  auto result = client->Call(
      "chat.send",
      {{"message", "Hello from OpenClaw"}, {"sessionKey", "oc:chat:test"}},
      10000);

  EXPECT_EQ(result["sessionKey"], "oc:chat:test");
  EXPECT_FALSE(result["response"].get<std::string>().empty());

  // Give events a moment to arrive
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // At minimum we should get a chat final event (the mock sends text + end)
  EXPECT_GE(chat_events.load(), 1);

  client->Disconnect();
}

// Test chat.history alias
TEST_F(RpcHandlersTest, ChatHistoryAlias) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  // Create a session with a message first
  client->Call("agent.request",
               {{"message", "Hi"}, {"sessionKey", "oc:hist:test"}}, 10000);

  // Call chat.history — new shape: {messages:[], thinkingLevel:null}
  auto result = client->Call("chat.history", {{"sessionKey", "oc:hist:test"}});

  ASSERT_TRUE(result.is_object());
  ASSERT_TRUE(result.contains("messages"));
  ASSERT_TRUE(result["messages"].is_array());
  EXPECT_GE(result["messages"].size(), 1u);

  client->Disconnect();
}

// Test models.list returns structured response with models array
TEST_F(RpcHandlersTest, ModelsListStub) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("models.list");

  ASSERT_TRUE(result.is_object());
  ASSERT_TRUE(result.contains("models"));
  ASSERT_TRUE(result["models"].is_array());
  EXPECT_GE(result["models"].size(), 1u);
  EXPECT_TRUE(result["models"][0].contains("id"));
  EXPECT_TRUE(result["models"][0].contains("active"));
  EXPECT_TRUE(result.contains("current"));
  EXPECT_TRUE(result.contains("aliases"));

  client->Disconnect();
}

// Test tools.catalog — new shape: {agentId, profiles:[], groups:[{tools:[]}]}
TEST_F(RpcHandlersTest, ToolsCatalogStub) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("tools.catalog", nlohmann::json::object());

  ASSERT_TRUE(result.is_object());
  EXPECT_TRUE(result.contains("agentId"));
  ASSERT_TRUE(result.contains("profiles"));
  ASSERT_TRUE(result["profiles"].is_array());
  ASSERT_TRUE(result.contains("groups"));
  ASSERT_TRUE(result["groups"].is_array());
  EXPECT_GE(result["groups"].size(), 1u);

  // Verify group structure
  for (const auto& group : result["groups"]) {
    EXPECT_TRUE(group.contains("id"));
    EXPECT_TRUE(group.contains("tools"));
    ASSERT_TRUE(group["tools"].is_array());
  }

  client->Disconnect();
}

TEST_F(RpcHandlersTest, EmbeddingsGenerateReturnsDeterministicVectors) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto first = client->Call("embeddings.generate",
                            {{"input", "market alpha beta"},
                             {"dimensions", 64},
                             {"encoding_format", "float"}},
                            5000);
  auto second = client->Call("embeddings.generate",
                             {{"input", "market alpha beta"},
                              {"dimensions", 64},
                              {"encoding_format", "float"}},
                             5000);

  ASSERT_TRUE(first.contains("data"));
  ASSERT_TRUE(first["data"].is_array());
  ASSERT_EQ(first["data"].size(), 1u);
  ASSERT_TRUE(first["data"][0]["embedding"].is_array());
  EXPECT_EQ(first["data"][0]["embedding"].size(), 64u);
  EXPECT_EQ(first["data"][0]["embedding"], second["data"][0]["embedding"]);
  EXPECT_GT(first["usage"]["total_tokens"].get<int>(), 0);

  client->Disconnect();
}

TEST_F(RpcHandlersTest, VectorRpcRoundTripWorks) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto generated = client->Call("embeddings.generate",
                                {{"input", "market alpha beta"},
                                 {"dimensions", 64},
                                 {"encoding_format", "float"}},
                                5000);
  auto vector = generated["data"][0]["embedding"];

  auto indexed = client->Call("vector.index",
                              {{"id", "doc-1"},
                               {"vector", vector},
                               {"text", "market alpha beta"},
                               {"metadata", {{"topic", "markets"}}}},
                              5000);
  EXPECT_TRUE(indexed["indexed"].get<bool>());
  EXPECT_FALSE(indexed["generated"].get<bool>());
  EXPECT_EQ(indexed["dimension"].get<int>(), 64);

  auto searched =
      client->Call("vector.search",
                   {{"vector", vector}, {"limit", 1}, {"threshold", 0.1}},
                   5000);
  ASSERT_EQ(searched["count"].get<int>(), 1);
  ASSERT_TRUE(searched["results"].is_array());
  ASSERT_EQ(searched["results"].size(), 1u);
  EXPECT_EQ(searched["results"][0]["id"].get<std::string>(), "doc-1");
  EXPECT_EQ(searched["results"][0]["metadata"]["topic"].get<std::string>(),
            "markets");

  auto deleted = client->Call("vector.delete", {{"id", "doc-1"}}, 5000);
  EXPECT_TRUE(deleted["deleted"].get<bool>());

  auto after_delete =
      client->Call("vector.search",
                   {{"vector", vector}, {"limit", 1}, {"threshold", 0.1}},
                   5000);
  EXPECT_EQ(after_delete["count"].get<int>(), 0);

  client->Disconnect();
}

TEST_F(RpcHandlersTest, EmbeddingsSearchFindsTextIndexedDocuments) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto first = client->Call("vector.index",
                            {{"id", "doc-market"},
                             {"text", "market alpha beta earnings call"},
                             {"metadata", {{"topic", "markets"}}}},
                            5000);
  auto second = client->Call("vector.index",
                             {{"id", "doc-weather"},
                              {"text", "weather storm alert forecast radar"},
                              {"metadata", {{"topic", "weather"}}}},
                             5000);
  EXPECT_TRUE(first["generated"].get<bool>());
  EXPECT_TRUE(second["generated"].get<bool>());

  auto search = client->Call(
      "embeddings.search",
      {{"query", "market alpha beta earnings call"},
       {"limit", 2},
       {"threshold", 0.05}},
      5000);

  ASSERT_GE(search["count"].get<int>(), 1);
  ASSERT_TRUE(search["results"].is_array());
  EXPECT_EQ(search["results"][0]["id"].get<std::string>(), "doc-market");
  EXPECT_EQ(search["results"][0]["metadata"]["topic"].get<std::string>(),
            "markets");

  client->Disconnect();
}
