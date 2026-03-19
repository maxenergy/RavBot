// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "ravbot/config.hpp"
#include "ravbot/core/agent_loop.hpp"
#include "ravbot/core/embedding_manager.hpp"
#include "ravbot/core/memory_manager.hpp"
#include "ravbot/core/mock_embedding_provider.hpp"
#include "ravbot/core/skill_loader.hpp"
#include "ravbot/core/usage_accumulator.hpp"
#include "ravbot/core/vector_database.hpp"
#include "ravbot/providers/llm_provider.hpp"
#include "ravbot/providers/provider_error.hpp"
#include "ravbot/providers/failover_resolver.hpp"
#include "ravbot/providers/provider_registry.hpp"
#include "ravbot/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

// Mock LLM provider that returns canned responses and captures requests
class MockLLMProvider : public ravbot::LLMProvider {
 public:
  std::string response_text = "I am RavBot.";
  std::string provider_name = "mock";
  mutable ravbot::ChatCompletionRequest last_request;

  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    last_request = request;
    ravbot::ChatCompletionResponse resp;
    resp.content = response_text;
    resp.finish_reason = "stop";
    resp.usage = mock_usage;
    return resp;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    last_request = request;
    ravbot::ChatCompletionResponse resp;
    resp.content = response_text;
    resp.is_stream_end = true;
    resp.usage = mock_usage;
    callback(resp);
  }

  std::string GetProviderName() const override {
    return provider_name;
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock-model"};
  }

  // Configurable token counts for usage-tracking tests (default 0 keeps
  // existing tests unaffected)
  ravbot::TokenUsage mock_usage;
};

class ErroringMockLLMProvider : public ravbot::LLMProvider {
 public:
  int call_count = 0;
  ravbot::ProviderError error{ravbot::ProviderErrorKind::kUnknown, 400,
                                 "Improperly formed request"};

  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest&) override {
    call_count++;
    throw error;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest&,
      std::function<void(const ravbot::ChatCompletionResponse&)>) override {
    call_count++;
    throw error;
  }

  std::string GetProviderName() const override {
    return "erroring-mock";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock-model"};
  }
};

class RetryThenSuccessMockLLMProvider : public ravbot::LLMProvider {
 public:
  int failures_before_success = 0;
  int call_count = 0;
  ravbot::ProviderError error{ravbot::ProviderErrorKind::kTimeout, 504,
                                 "temporary timeout"};

  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    last_request = request;
    call_count++;
    if (call_count <= failures_before_success) {
      throw error;
    }

    ravbot::ChatCompletionResponse resp;
    resp.content = "recovered";
    resp.finish_reason = "stop";
    return resp;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)>) override {
    last_request = request;
    call_count++;
    if (call_count <= failures_before_success) {
      throw error;
    }
  }

  std::string GetProviderName() const override {
    return provider_name;
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock-model"};
  }

  std::string provider_name = "retry-mock";
  ravbot::ChatCompletionRequest last_request;
};

class ToolReplayFilteringMockProvider : public ravbot::LLMProvider {
 public:
  std::vector<ravbot::ChatCompletionRequest> requests;

  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    requests.push_back(request);

    ravbot::ChatCompletionResponse resp;
    if (requests.size() == 1) {
      ravbot::ToolCall tc;
      tc.id = "tooluse_read123";
      tc.name = "read";
      tc.arguments = {{"path", read_path}};
      resp.content = "I'll inspect that file.";
      resp.tool_calls = {tc};
      resp.finish_reason = "tool_calls";
    } else {
      resp.content = "Done.";
      resp.finish_reason = "stop";
    }
    return resp;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest&,
      std::function<void(const ravbot::ChatCompletionResponse&)>) override {}

  std::string GetProviderName() const override {
    return "anthropic";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock-model"};
  }

  std::string read_path;
};

class LookupRetryMockProvider : public ravbot::LLMProvider {
 public:
  std::vector<ravbot::ChatCompletionRequest> requests;

  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    requests.push_back(request);

    ravbot::ChatCompletionResponse resp;
    if (requests.size() == 1) {
      resp.content =
          u8"根据搜索结果，我找到了几个与 Obsidian 相关的 GitHub "
          u8"项目。让我为你获取更详细的信息，特别是关于长效记忆功能的插件：";
      resp.finish_reason = "stop";
    } else {
      resp.content =
          "1. "
          "example/obsidian-memory\nhttps://github.com/example/obsidian-memory";
      resp.finish_reason = "stop";
    }
    return resp;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest&,
      std::function<void(const ravbot::ChatCompletionResponse&)>) override {}

  std::string GetProviderName() const override {
    return "anthropic";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock-model"};
  }
};

class WebSearchReplayMockProvider : public ravbot::LLMProvider {
 public:
  std::vector<ravbot::ChatCompletionRequest> requests;
  bool return_raw_dump_on_second = false;

  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    requests.push_back(request);

    ravbot::ChatCompletionResponse resp;
    if (requests.size() == 1) {
      ravbot::ToolCall tc;
      tc.id = "tooluse_web123";
      tc.name = "web_search";
      tc.arguments = {{"query", "obsidian long-term memory plugin github"},
                      {"count", 3}};
      resp.content = "Let me search GitHub for that.";
      resp.tool_calls = {tc};
      resp.finish_reason = "tool_calls";
    } else if (requests.size() == 2 && return_raw_dump_on_second) {
      resp.content =
          "Here are the search results for \"obsidian long-term memory\":\n\n"
          "1. **obsidian · GitHub Topics · GitHub**\n"
          "   Obsidian is a powerful knowledge base.\n"
          "   Source: https://github.com/topics/obsidian\n\n"
          "2. **logancyang/obsidian-copilot**\n"
          "   THE Copilot in Obsidian.\n"
          "   Source: https://github.com/logancyang/obsidian-copilot\n\n"
          "Please note that these are web search results and may not be "
          "fully accurate or up-to-date.";
      resp.finish_reason = "stop";
    } else {
      resp.content =
          "Most relevant: example/obsidian-memory, because it focuses on "
          "long-term memory for agent workflows.";
      resp.finish_reason = "stop";
    }
    return resp;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest&,
      std::function<void(const ravbot::ChatCompletionResponse&)>) override {}

  std::string GetProviderName() const override {
    return "anthropic";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock-model"};
  }
};

class AgentLoopTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = ravbot::test::MakeTestDir("ravbot_agent_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    memory_manager_ =
        std::make_shared<ravbot::MemoryManager>(test_dir_, logger_);
    skill_loader_ = std::make_shared<ravbot::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<ravbot::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    mock_provider_ = std::make_shared<MockLLMProvider>();

    ravbot::AgentConfig agent_config;
    agent_config.model = "test-model";
    agent_config.temperature = 0.5;
    agent_config.max_tokens = 2048;
    agent_config.max_iterations = 15;

    agent_loop_ = std::make_unique<ravbot::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_provider_,
        agent_config, logger_);
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::shared_ptr<ravbot::EmbeddingManager> CreateEmbeddingManager(
      int dimension = 128) {
    auto registry = std::make_shared<ravbot::EmbeddingProviderRegistry>();
    registry->RegisterProvider(
        "mock", std::make_shared<ravbot::MockEmbeddingProvider>(dimension));
    registry->SetDefaultProvider("mock");

    ravbot::VectorDatabaseConfig vector_config;
    vector_config.use_hnsw = false;
    auto vector_db = std::make_shared<ravbot::VectorDatabase>(
        (test_dir_ / "embeddings.db").string(), logger_, vector_config);
    EXPECT_TRUE(vector_db->Initialize());

    auto manager =
        std::make_shared<ravbot::EmbeddingManager>(registry, vector_db, logger_);
    manager->SetProvider("mock");
    return manager;
  }

  std::filesystem::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<ravbot::MemoryManager> memory_manager_;
  std::shared_ptr<ravbot::SkillLoader> skill_loader_;
  std::shared_ptr<ravbot::ToolRegistry> tool_registry_;
  std::shared_ptr<MockLLMProvider> mock_provider_;
  std::unique_ptr<ravbot::AgentLoop> agent_loop_;
};

TEST_F(AgentLoopTest, ProcessMessageReturnsResponse) {
  mock_provider_->response_text = "Hello! I am RavBot.";

  auto new_msgs =
      agent_loop_->ProcessMessage("Hello", {}, "You are a helpful assistant.");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().content[0].text, "Hello! I am RavBot.");
}

TEST_F(AgentLoopTest, ProcessMessageWithHistory) {
  ravbot::Message prev_user{"user", "What is your name?"};
  ravbot::Message prev_assistant{"assistant", "I am RavBot."};

  std::vector<ravbot::Message> history = {prev_user, prev_assistant};

  mock_provider_->response_text = "You asked about my name.";

  auto new_msgs = agent_loop_->ProcessMessage("What did I just ask?", history,
                                              "You are helpful.");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().content[0].text, "You asked about my name.");
}

TEST_F(AgentLoopTest, ProcessMessageWithEmptySystemPrompt) {
  mock_provider_->response_text = "Response without system prompt.";

  auto new_msgs = agent_loop_->ProcessMessage("Test", {}, "");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().content[0].text, "Response without system prompt.");
}

TEST_F(AgentLoopTest, StreamingCallback) {
  mock_provider_->response_text = "Streamed response.";

  std::vector<ravbot::AgentEvent> events;
  agent_loop_->ProcessMessageStream(
      "Hello", {}, "System.", [&events](const ravbot::AgentEvent& event) {
        events.push_back(event);
      });

  // Should have at least a text_delta and message_end
  EXPECT_FALSE(events.empty());
}

TEST_F(AgentLoopTest, StopInterruptsProcessing) {
  agent_loop_->Stop();

  auto new_msgs = agent_loop_->ProcessMessage("Hello", {}, "System.");

  // Should return stop message or the mock response (depending on timing)
  EXPECT_FALSE(new_msgs.empty());
}

TEST_F(AgentLoopTest, SetMaxIterations) {
  agent_loop_->SetMaxIterations(3);
  // Should not throw
  mock_provider_->response_text = "ok";
  auto new_msgs = agent_loop_->ProcessMessage("test", {}, "sys");
  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().content[0].text, "ok");
}

TEST_F(AgentLoopTest, ProcessMessageNormalizesModelAliasBeforeProviderCall) {
  auto registry = std::make_unique<ravbot::ProviderRegistry>(logger_);
  auto resolved_provider = std::make_shared<MockLLMProvider>();
  resolved_provider->provider_name = "anthropic";
  resolved_provider->response_text = "alias ok";

  registry->RegisterFactory(
      "anthropic",
      [resolved_provider](const ravbot::ProviderEntry& /*entry*/,
                          std::shared_ptr<spdlog::logger> /*logger*/)
          -> std::shared_ptr<ravbot::LLMProvider> {
        return resolved_provider;
      });

  ravbot::ProviderEntry entry;
  entry.id = "anthropic";
  entry.api_key = "test-key";
  registry->AddProvider(entry);
  registry->AddAlias("sonnet", "anthropic/claude-sonnet-4-5");

  agent_loop_->SetProviderRegistry(registry.get());
  agent_loop_->SetModel("sonnet");

  auto new_msgs = agent_loop_->ProcessMessage("Hello", {}, "You are helpful.");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(resolved_provider->last_request.model, "claude-sonnet-4-5");
  EXPECT_EQ(agent_loop_->GetConfig().model, "sonnet");
}

TEST_F(AgentLoopTest, ProcessMessageStripsProviderPrefixBeforeProviderCall) {
  auto registry = std::make_unique<ravbot::ProviderRegistry>(logger_);
  auto resolved_provider = std::make_shared<MockLLMProvider>();
  resolved_provider->provider_name = "openai";
  resolved_provider->response_text = "ref ok";

  registry->RegisterFactory(
      "openai",
      [resolved_provider](const ravbot::ProviderEntry& /*entry*/,
                          std::shared_ptr<spdlog::logger> /*logger*/)
          -> std::shared_ptr<ravbot::LLMProvider> {
        return resolved_provider;
      });

  ravbot::ProviderEntry entry;
  entry.id = "openai";
  entry.api_key = "test-key";
  registry->AddProvider(entry);

  agent_loop_->SetProviderRegistry(registry.get());
  agent_loop_->SetModel("openai/gpt-4o");

  auto new_msgs = agent_loop_->ProcessMessage("Hello", {}, "You are helpful.");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(resolved_provider->last_request.model, "gpt-4o");
  EXPECT_EQ(agent_loop_->GetConfig().model, "openai/gpt-4o");
}

TEST_F(AgentLoopTest, NonRetryableProviderErrorThrowsImmediately) {
  auto error_provider = std::make_shared<ErroringMockLLMProvider>();

  ravbot::AgentConfig agent_config;
  agent_config.model = "test-model";
  agent_config.temperature = 0.5;
  agent_config.max_tokens = 2048;
  agent_config.max_iterations = 5;

  ravbot::AgentLoop loop(memory_manager_, skill_loader_, tool_registry_,
                            error_provider, agent_config, logger_);

  EXPECT_THROW(loop.ProcessMessage("Hello", {}, "System"),
               ravbot::ProviderError);
  EXPECT_EQ(error_provider->call_count, 1);
}

TEST_F(AgentLoopTest, RetryableProviderErrorRetriesUntilSuccess) {
  auto retry_provider = std::make_shared<RetryThenSuccessMockLLMProvider>();
  retry_provider->failures_before_success = 2;

  ravbot::AgentConfig agent_config;
  agent_config.model = "test-model";
  agent_config.temperature = 0.5;
  agent_config.max_tokens = 2048;
  agent_config.max_iterations = 5;

  ravbot::AgentLoop loop(memory_manager_, skill_loader_, tool_registry_,
                            retry_provider, agent_config, logger_);

  auto new_msgs = loop.ProcessMessage("Hello", {}, "System");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().text(), "recovered");
  EXPECT_EQ(retry_provider->call_count, 3);
}

TEST_F(AgentLoopTest, FailoverResolverSwitchesToFallbackProvider) {
  auto registry = std::make_unique<ravbot::ProviderRegistry>(logger_);

  registry->RegisterFactory(
      "primary",
      [](const ravbot::ProviderEntry&,
         std::shared_ptr<spdlog::logger>) -> std::shared_ptr<ravbot::LLMProvider> {
        auto provider = std::make_shared<ErroringMockLLMProvider>();
        provider->error = ravbot::ProviderError(
            ravbot::ProviderErrorKind::kAuthError, 401, "bad primary key");
        return provider;
      });
  registry->RegisterFactory(
      "backup",
      [](const ravbot::ProviderEntry&,
         std::shared_ptr<spdlog::logger>) -> std::shared_ptr<ravbot::LLMProvider> {
        auto provider = std::make_shared<MockLLMProvider>();
        provider->provider_name = "backup";
        provider->response_text = "fallback response";
        return provider;
      });

  ravbot::ProviderEntry primary_entry;
  primary_entry.id = "primary";
  primary_entry.api_key = "primary-key";
  registry->AddProvider(primary_entry);

  ravbot::ProviderEntry backup_entry;
  backup_entry.id = "backup";
  backup_entry.api_key = "backup-key";
  registry->AddProvider(backup_entry);

  auto failover_resolver =
      std::make_unique<ravbot::FailoverResolver>(registry.get(), logger_);
  failover_resolver->SetFallbackChain({"backup/mock-model"});

  ravbot::AgentConfig agent_config;
  agent_config.model = "primary/mock-model";
  agent_config.temperature = 0.5;
  agent_config.max_tokens = 2048;
  agent_config.max_iterations = 5;

  auto primary_provider = registry->GetProvider("primary");
  ASSERT_NE(primary_provider, nullptr);

  ravbot::AgentLoop loop(memory_manager_, skill_loader_, tool_registry_,
                            primary_provider, agent_config, logger_);
  loop.SetProviderRegistry(registry.get());
  loop.SetFailoverResolver(failover_resolver.get());

  auto new_msgs = loop.ProcessMessage("Hello", {}, "System");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().text(), "fallback response");
}

TEST_F(AgentLoopTest, StopInterruptsRetryBackoff) {
  auto retry_provider = std::make_shared<RetryThenSuccessMockLLMProvider>();
  retry_provider->failures_before_success = 10;

  ravbot::AgentConfig agent_config;
  agent_config.model = "test-model";
  agent_config.temperature = 0.5;
  agent_config.max_tokens = 2048;
  agent_config.max_iterations = 5;

  ravbot::AgentLoop loop(memory_manager_, skill_loader_, tool_registry_,
                            retry_provider, agent_config, logger_);

  std::exception_ptr worker_error;
  std::thread worker([&]() {
    try {
      loop.ProcessMessage("Hello", {}, "System");
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  loop.Stop();
  worker.join();

  ASSERT_NE(worker_error, nullptr);
  EXPECT_TRUE(loop.IsAborted());
  try {
    std::rethrow_exception(worker_error);
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("stopped by user"), std::string::npos);
  }
}

// --- AgentConfig injection tests ---

TEST_F(AgentLoopTest, UsesConfigModel) {
  mock_provider_->response_text = "ok";
  agent_loop_->ProcessMessage("test", {}, "sys");

  EXPECT_EQ(mock_provider_->last_request.model, "test-model");
}

TEST_F(AgentLoopTest, UsesConfigTemperature) {
  mock_provider_->response_text = "ok";
  agent_loop_->ProcessMessage("test", {}, "sys");

  EXPECT_DOUBLE_EQ(mock_provider_->last_request.temperature, 0.5);
}

TEST_F(AgentLoopTest, UsesConfigMaxTokens) {
  mock_provider_->response_text = "ok";
  agent_loop_->ProcessMessage("test", {}, "sys");

  EXPECT_EQ(mock_provider_->last_request.max_tokens, 2048);
}

TEST_F(AgentLoopTest, SetConfigUpdatesModel) {
  ravbot::AgentConfig new_config;
  new_config.model = "new-model";
  new_config.temperature = 0.9;
  new_config.max_tokens = 8192;
  new_config.max_iterations = 5;

  agent_loop_->SetConfig(new_config);

  mock_provider_->response_text = "ok";
  agent_loop_->ProcessMessage("test", {}, "sys");

  EXPECT_EQ(mock_provider_->last_request.model, "new-model");
  EXPECT_DOUBLE_EQ(mock_provider_->last_request.temperature, 0.9);
  EXPECT_EQ(mock_provider_->last_request.max_tokens, 8192);
}

TEST_F(AgentLoopTest, StreamingUsesConfigModel) {
  mock_provider_->response_text = "streamed";
  std::vector<ravbot::AgentEvent> events;
  agent_loop_->ProcessMessageStream(
      "test", {}, "sys", [&events](const ravbot::AgentEvent& event) {
        events.push_back(event);
      });

  EXPECT_EQ(mock_provider_->last_request.model, "test-model");
  EXPECT_TRUE(mock_provider_->last_request.stream);
}

TEST_F(AgentLoopTest, GetConfigReturnsCurrentConfig) {
  const auto& config = agent_loop_->GetConfig();
  EXPECT_EQ(config.model, "test-model");
  EXPECT_DOUBLE_EQ(config.temperature, 0.5);
  EXPECT_EQ(config.max_tokens, 2048);
}

// --- Streaming returns new messages ---

TEST_F(AgentLoopTest, StreamReturnsNewMessages) {
  mock_provider_->response_text = "Final answer.";

  std::vector<ravbot::AgentEvent> events;
  auto new_msgs = agent_loop_->ProcessMessageStream(
      "Hello", {}, "System.", [&events](const ravbot::AgentEvent& event) {
        events.push_back(event);
      });

  // Should have at least 1 assistant message (the final response)
  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().role, "assistant");
  EXPECT_FALSE(new_msgs.back().content.empty());
  EXPECT_EQ(new_msgs.back().content[0].type, "text");
  EXPECT_EQ(new_msgs.back().content[0].text, "Final answer.");
}

TEST_F(AgentLoopTest, NonStreamReturnsNewMessages) {
  mock_provider_->response_text = "Non-stream final.";

  auto new_msgs = agent_loop_->ProcessMessage("Hello", {}, "System.");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().role, "assistant");
  EXPECT_EQ(new_msgs.back().content[0].text, "Non-stream final.");
}

TEST_F(AgentLoopTest, ContextPrunerCompressesHistoryBeforeNonStreamingRequest) {
  ravbot::AgentConfig config = agent_loop_->GetConfig();
  config.context_window = ravbot::kContextWindow32K;
  config.max_tokens = 1024;
  config.auto_compact = false;
  agent_loop_->SetConfig(config);

  std::vector<ravbot::Message> history;
  std::string large_tool_result;
  for (int line = 0; line < 400; ++line) {
    large_tool_result += "line " + std::to_string(line) + " ";
    large_tool_result += std::string(40, 'x') + "\n";
  }
  for (int i = 0; i < 8; ++i) {
    ravbot::Message assistant;
    assistant.role = "assistant";
    assistant.content.push_back(ravbot::ContentBlock::MakeText("Thinking"));
    assistant.content.push_back(ravbot::ContentBlock::MakeToolUse(
        "tool_" + std::to_string(i), "read", {{"path", "/tmp/demo"}}));
    history.push_back(assistant);

    ravbot::Message tool_result;
    tool_result.role = "user";
    tool_result.content.push_back(ravbot::ContentBlock::MakeToolResult(
        "tool_" + std::to_string(i), large_tool_result));
    history.push_back(tool_result);
  }

  auto new_msgs =
      agent_loop_->ProcessMessage("continue", history, "System prompt.");

  ASSERT_FALSE(new_msgs.empty());

  int pruned_tool_results = 0;
  for (const auto& msg : mock_provider_->last_request.messages) {
    for (const auto& block : msg.content) {
      if (block.type == "tool_result" &&
          (block.content.find("lines omitted") != std::string::npos ||
           block.content.find("omitted") != std::string::npos)) {
        pruned_tool_results++;
      }
    }
  }

  EXPECT_GT(pruned_tool_results, 0);
}

TEST_F(AgentLoopTest, ContextPrunerCompressesHistoryBeforeStreamingRequest) {
  ravbot::AgentConfig config = agent_loop_->GetConfig();
  config.context_window = ravbot::kContextWindow32K;
  config.max_tokens = 1024;
  config.auto_compact = false;
  agent_loop_->SetConfig(config);

  std::vector<ravbot::Message> history;
  std::string large_tool_result;
  for (int line = 0; line < 400; ++line) {
    large_tool_result += "line " + std::to_string(line) + " ";
    large_tool_result += std::string(40, 'y') + "\n";
  }
  for (int i = 0; i < 8; ++i) {
    ravbot::Message assistant;
    assistant.role = "assistant";
    assistant.content.push_back(
        ravbot::ContentBlock::MakeText("Inspecting"));
    assistant.content.push_back(ravbot::ContentBlock::MakeToolUse(
        "stream_tool_" + std::to_string(i), "read", {{"path", "/tmp/demo"}}));
    history.push_back(assistant);

    ravbot::Message tool_result;
    tool_result.role = "user";
    tool_result.content.push_back(ravbot::ContentBlock::MakeToolResult(
        "stream_tool_" + std::to_string(i), large_tool_result));
    history.push_back(tool_result);
  }

  std::vector<ravbot::AgentEvent> events;
  agent_loop_->ProcessMessageStream(
      "continue", history, "System prompt.",
      [&events](const ravbot::AgentEvent& event) {
        events.push_back(event);
      });

  EXPECT_FALSE(events.empty());

  int pruned_tool_results = 0;
  for (const auto& msg : mock_provider_->last_request.messages) {
    for (const auto& block : msg.content) {
      if (block.type == "tool_result" &&
          (block.content.find("lines omitted") != std::string::npos ||
           block.content.find("omitted") != std::string::npos)) {
        pruned_tool_results++;
      }
    }
  }

  EXPECT_GT(pruned_tool_results, 0);
}

TEST_F(AgentLoopTest, AnthropicSanitizesDanglingToolUseAndMergesUserTurns) {
  mock_provider_->provider_name = "anthropic";
  mock_provider_->response_text = "ok";

  ravbot::Message prior_user{"user", "Earlier user context."};

  ravbot::Message dangling_assistant;
  dangling_assistant.role = "assistant";
  dangling_assistant.content.push_back(
      ravbot::ContentBlock::MakeText("Let me inspect that."));
  dangling_assistant.content.push_back(ravbot::ContentBlock::MakeToolUse(
      "call_1", "read_file", {{"path", "/tmp/demo"}}));

  ravbot::Message unrelated_user;
  unrelated_user.role = "user";
  unrelated_user.content.push_back(
      ravbot::ContentBlock::MakeText("Thanks."));
  unrelated_user.content.push_back(
      ravbot::ContentBlock::MakeToolResult("other_call", "orphaned result"));

  std::vector<ravbot::Message> history = {prior_user, dangling_assistant,
                                             unrelated_user};

  agent_loop_->ProcessMessage("Continue with that.", history, "System.");

  const auto& sent = mock_provider_->last_request.messages;
  ASSERT_EQ(sent.size(), 3u);
  EXPECT_EQ(sent[0].role, "system");
  ASSERT_EQ(sent[1].role, "assistant");
  ASSERT_EQ(sent[1].content.size(), 1u);
  EXPECT_EQ(sent[1].content[0].type, "text");
  EXPECT_EQ(sent[1].content[0].text, "Let me inspect that.");
  ASSERT_EQ(sent[2].role, "user");
  ASSERT_EQ(sent[2].content.size(), 2u);
  EXPECT_EQ(sent[2].content[0].type, "text");
  EXPECT_EQ(sent[2].content[0].text, "Thanks.");
  EXPECT_EQ(sent[2].content[1].type, "text");
  EXPECT_EQ(sent[2].content[1].text, "Continue with that.");
}

TEST_F(AgentLoopTest, AnthropicSanitizationAlsoAppliesToStreamingRequests) {
  mock_provider_->provider_name = "anthropic";
  mock_provider_->response_text = "stream ok";

  ravbot::Message assistant;
  assistant.role = "assistant";
  assistant.content.push_back(ravbot::ContentBlock::MakeToolUse(
      "call_stream", "search", {{"query", "demo"}}));

  ravbot::Message user_with_orphan_result;
  user_with_orphan_result.role = "user";
  user_with_orphan_result.content.push_back(
      ravbot::ContentBlock::MakeToolResult("wrong_id", "bad result"));
  user_with_orphan_result.content.push_back(
      ravbot::ContentBlock::MakeText("follow-up"));

  std::vector<ravbot::AgentEvent> events;
  agent_loop_->ProcessMessageStream(
      "continue", {assistant, user_with_orphan_result}, "System.",
      [&events](const ravbot::AgentEvent& event) {
        events.push_back(event);
      });

  const auto& sent = mock_provider_->last_request.messages;
  auto first_non_system = std::find_if(
      sent.begin(), sent.end(),
      [](const ravbot::Message& msg) { return msg.role != "system"; });
  ASSERT_NE(first_non_system, sent.end());
  EXPECT_EQ(first_non_system->role, "user");
  ASSERT_EQ(first_non_system->content.size(), 2u);
  EXPECT_EQ(first_non_system->content[0].type, "text");
  EXPECT_EQ(first_non_system->content[0].text, "follow-up");
  EXPECT_EQ(first_non_system->content[1].type, "text");
  EXPECT_EQ(first_non_system->content[1].text, "continue");
}

TEST_F(AgentLoopTest,
       AnthropicCollapsesCompletedHistoricalToolTurnsBeforeFollowUp) {
  mock_provider_->provider_name = "anthropic";
  mock_provider_->response_text = "ok";

  ravbot::Message prior_user{"user", "Search for world news."};

  ravbot::Message tool_use_assistant;
  tool_use_assistant.role = "assistant";
  tool_use_assistant.content.push_back(
      ravbot::ContentBlock::MakeText("Let me search for that."));
  tool_use_assistant.content.push_back(ravbot::ContentBlock::MakeToolUse(
      "tool_1", "web_search", {{"query", "world news"}}));

  ravbot::Message tool_result_user;
  tool_result_user.role = "user";
  tool_result_user.content.push_back(
      ravbot::ContentBlock::MakeToolResult("tool_1", "search results"));

  ravbot::Message final_assistant{"assistant", "Here is the summary."};

  std::vector<ravbot::Message> history = {
      prior_user,
      tool_use_assistant,
      tool_result_user,
      final_assistant,
  };

  agent_loop_->ProcessMessage("What else can you do?", history, "System.");

  const auto& sent = mock_provider_->last_request.messages;
  ASSERT_EQ(sent.size(), 4u);
  EXPECT_EQ(sent[0].role, "system");
  EXPECT_EQ(sent[1].role, "user");
  EXPECT_EQ(sent[1].text(), "Search for world news.");
  EXPECT_EQ(sent[2].role, "assistant");
  ASSERT_EQ(sent[2].content.size(), 1u);
  EXPECT_EQ(sent[2].content[0].type, "text");
  EXPECT_EQ(sent[2].content[0].text, "Here is the summary.");
  EXPECT_EQ(sent[3].role, "user");
  ASSERT_EQ(sent[3].content.size(), 1u);
  EXPECT_EQ(sent[3].content[0].type, "text");
  EXPECT_EQ(sent[3].content[0].text, "What else can you do?");
}

TEST_F(AgentLoopTest, AnthropicReplayKeepsFullToolListToMatchSystemPrompt) {
  auto replay_provider = std::make_shared<ToolReplayFilteringMockProvider>();

  const auto read_file = test_dir_ / "replay-read-target.txt";
  {
    std::ofstream out(read_file);
    out << "hello";
  }
  replay_provider->read_path = read_file.string();

  ravbot::AgentConfig agent_config;
  agent_config.model = "test-model";
  agent_config.temperature = 0.5;
  agent_config.max_tokens = 2048;
  agent_config.max_iterations = 15;

  ravbot::AgentLoop replay_loop(memory_manager_, skill_loader_,
                                   tool_registry_, replay_provider,
                                   agent_config, logger_);

  auto new_msgs =
      replay_loop.ProcessMessage("Please inspect the file.", {}, "System.");

  ASSERT_FALSE(new_msgs.empty());
  ASSERT_EQ(replay_provider->requests.size(), 2u);
  EXPECT_GT(replay_provider->requests[0].tools.size(), 1u);
  EXPECT_EQ(replay_provider->requests[1].tools.size(),
            replay_provider->requests[0].tools.size());
  bool found_read = false;
  for (const auto& tool : replay_provider->requests[1].tools) {
    if (tool.contains("function") && tool["function"].contains("name") &&
        tool["function"]["name"] == "read") {
      found_read = true;
      break;
    }
  }
  EXPECT_TRUE(found_read);
}

TEST_F(AgentLoopTest,
       AnthropicReplayKeepsOnlyCurrentUserTurnDuringToolResultFollowUp) {
  auto replay_provider = std::make_shared<ToolReplayFilteringMockProvider>();

  const auto read_file = test_dir_ / "replay-current-turn.txt";
  {
    std::ofstream out(read_file);
    out << "obsidian integration notes";
  }
  replay_provider->read_path = read_file.string();

  ravbot::AgentConfig agent_config;
  agent_config.model = "test-model";
  agent_config.temperature = 0.5;
  agent_config.max_tokens = 2048;
  agent_config.max_iterations = 15;

  ravbot::AgentLoop replay_loop(memory_manager_, skill_loader_,
                                   tool_registry_, replay_provider,
                                   agent_config, logger_);

  std::vector<ravbot::Message> history = {
      ravbot::Message{"user", "你有什么技能？"},
      ravbot::Message{"assistant",
                         "我可以帮助你写代码、运行测试和搜索资料。"},
      ravbot::Message{"user",
                         "有什么方法可以让你获得长效记忆以及快速检索？"},
      ravbot::Message{"assistant",
                         "我可以通过结构化记忆系统来增强长期记忆。"},
  };

  auto new_msgs = replay_loop.ProcessMessage("帮我详细研究一下obsidian的接入",
                                             history, "System.");

  ASSERT_FALSE(new_msgs.empty());
  ASSERT_EQ(replay_provider->requests.size(), 2u);

  const auto& sent = replay_provider->requests[1].messages;
  ASSERT_EQ(sent.size(), 4u);
  EXPECT_EQ(sent[0].role, "system");
  EXPECT_EQ(sent[1].role, "user");
  EXPECT_EQ(sent[1].text(), "帮我详细研究一下obsidian的接入");

  ASSERT_EQ(sent[2].role, "assistant");
  ASSERT_EQ(sent[2].content.size(), 2u);
  EXPECT_EQ(sent[2].content[0].type, "text");
  EXPECT_EQ(sent[2].content[0].text, "I'll inspect that file.");
  EXPECT_EQ(sent[2].content[1].type, "tool_use");
  EXPECT_EQ(sent[2].content[1].name, "read");

  ASSERT_EQ(sent[3].role, "user");
  ASSERT_EQ(sent[3].content.size(), 1u);
  EXPECT_EQ(sent[3].content[0].type, "tool_result");
  EXPECT_EQ(sent[3].content[0].tool_use_id, "tooluse_read123");
  EXPECT_EQ(sent[3].content[0].content, "obsidian integration notes");

  for (size_t i = 1; i < sent.size(); ++i) {
    EXPECT_EQ(sent[i].text().find("你有什么技能"), std::string::npos);
    EXPECT_EQ(sent[i].text().find("长效记忆"), std::string::npos);
  }
}

TEST_F(AgentLoopTest, AnthropicFollowUpDoesNotStartWithAssistantTurn) {
  mock_provider_->provider_name = "anthropic";
  mock_provider_->response_text = "ok";

  std::vector<ravbot::Message> history = {
      ravbot::Message{"assistant", "There is a tool use."},
      ravbot::Message{"assistant", "找到了！"},
  };

  agent_loop_->ProcessMessage("继续", history, "System.");

  const auto& sent = mock_provider_->last_request.messages;
  auto first_non_system = std::find_if(
      sent.begin(), sent.end(),
      [](const ravbot::Message& msg) { return msg.role != "system"; });
  ASSERT_NE(first_non_system, sent.end());
  EXPECT_EQ(first_non_system->role, "user");
  EXPECT_EQ(first_non_system->text(), "继续");
}

TEST_F(AgentLoopTest, BriefContinuationPromptsSuppressDuplicateNotice) {
  auto embedding_manager = CreateEmbeddingManager();
  agent_loop_->SetEmbeddingManager(embedding_manager);
  mock_provider_->response_text = "ok";

  const std::vector<std::string> prompts = {"continue", u8"请继续"};
  for (size_t i = 0; i < prompts.size(); ++i) {
    const auto& prompt = prompts[i];
    const auto session_key = "dup-continue-" + std::to_string(i);
    const std::string metadata =
        std::string("{\"role\":\"user\",\"session\":\"") + session_key +
        "\"}";
    ASSERT_TRUE(embedding_manager->IndexText(
        "seed-" + std::to_string(i), prompt, metadata));

    agent_loop_->ProcessMessage(prompt, {}, "System.", session_key);
    const auto& sent = mock_provider_->last_request.messages;

    bool has_duplicate_notice = false;
    for (const auto& msg : sent) {
      if (msg.role == "system" &&
          msg.text().find("very similar message") != std::string::npos) {
        has_duplicate_notice = true;
      }
    }
    EXPECT_FALSE(has_duplicate_notice) << prompt;
  }
}

TEST_F(AgentLoopTest,
       FollowUpCollapsesCompletedToolTurnToQuestionAndFinalAnswer) {
  mock_provider_->response_text = "ok";

  ravbot::Message question;
  question.role = "user";
  question.content.push_back(
      ravbot::ContentBlock::MakeText("你的记忆有什么？"));

  ravbot::Message assistant_tool;
  assistant_tool.role = "assistant";
  assistant_tool.content.push_back(
      ravbot::ContentBlock::MakeText("我先读取一下记忆文件。"));
  assistant_tool.content.push_back(ravbot::ContentBlock::MakeToolUse(
      "call_mem_1", "memory_get", {{"path", "MEMORY.md"}}));

  ravbot::Message tool_result;
  tool_result.role = "user";
  tool_result.content.push_back(ravbot::ContentBlock::MakeToolResult(
      "call_mem_1", std::string(4000, 'm')));

  ravbot::Message final_answer;
  final_answer.role = "assistant";
  final_answer.content.push_back(ravbot::ContentBlock::MakeText(
      "你好！我的记忆里主要记录了项目更名、工作区路径和一些运行时信息。"));

  std::vector<ravbot::Message> history = {
      question, assistant_tool, tool_result, final_answer};

  agent_loop_->ProcessMessage("请继续解释刚才的记忆内容", history, "System.");

  const auto& sent = mock_provider_->last_request.messages;
  ASSERT_EQ(sent.size(), 4u);
  EXPECT_EQ(sent[0].role, "system");
  EXPECT_EQ(sent[1].role, "user");
  EXPECT_EQ(sent[1].text(), "你的记忆有什么？");
  EXPECT_EQ(sent[2].role, "assistant");
  EXPECT_EQ(sent[2].text(),
            "你好！我的记忆里主要记录了项目更名、工作区路径和一些运行时信息。");
  EXPECT_EQ(sent[3].role, "user");
  EXPECT_EQ(sent[3].text(), "请继续解释刚才的记忆内容");

  for (const auto& msg : sent) {
    for (const auto& block : msg.content) {
      EXPECT_NE(block.type, "tool_use");
      EXPECT_NE(block.type, "tool_result");
    }
  }
}

TEST_F(AgentLoopTest, RepeatedQuestionsStillAddDuplicateNotice) {
  auto embedding_manager = CreateEmbeddingManager();
  agent_loop_->SetEmbeddingManager(embedding_manager);
  mock_provider_->response_text = "ok";

  const std::string session_key = "dup-question";
  ASSERT_TRUE(embedding_manager->IndexText(
      "seed-question", "What is RavBot?",
      std::string("{\"role\":\"user\",\"session\":\"") + session_key +
          "\"}"));

  agent_loop_->ProcessMessage("What is RavBot?", {}, "System.", session_key);
  const auto& sent = mock_provider_->last_request.messages;

  bool has_duplicate_notice = false;
  for (const auto& msg : sent) {
    if (msg.role == "system" &&
        msg.text().find("very similar message") != std::string::npos) {
      has_duplicate_notice = true;
    }
  }
  EXPECT_TRUE(has_duplicate_notice);
}

TEST_F(AgentLoopTest, NonAnthropicRequestsAlsoRepairOrphanToolResults) {
  mock_provider_->provider_name = "openai";
  mock_provider_->response_text = "ok";

  ravbot::Message assistant;
  assistant.role = "assistant";
  assistant.content.push_back(
      ravbot::ContentBlock::MakeText("I called a tool."));
  assistant.content.push_back(ravbot::ContentBlock::MakeToolUse(
      "call_openai", "read", {{"path", "/tmp/demo"}}));

  ravbot::Message orphan_user;
  orphan_user.role = "user";
  orphan_user.content.push_back(
      ravbot::ContentBlock::MakeToolResult("wrong_call", "bad result"));
  orphan_user.content.push_back(
      ravbot::ContentBlock::MakeText("follow-up"));

  agent_loop_->ProcessMessage("continue", {assistant, orphan_user}, "System.");

  const auto& sent = mock_provider_->last_request.messages;
  ASSERT_EQ(sent.size(), 4u);
  EXPECT_EQ(sent[0].role, "system");
  EXPECT_EQ(sent[1].role, "assistant");
  ASSERT_EQ(sent[1].content.size(), 1u);
  EXPECT_EQ(sent[1].content[0].type, "text");
  EXPECT_EQ(sent[1].content[0].text, "I called a tool.");
  EXPECT_EQ(sent[2].role, "user");
  ASSERT_EQ(sent[2].content.size(), 1u);
  EXPECT_EQ(sent[2].content[0].type, "text");
  EXPECT_EQ(sent[2].content[0].text, "follow-up");
  EXPECT_EQ(sent[3].role, "user");
  ASSERT_EQ(sent[3].content.size(), 1u);
  EXPECT_EQ(sent[3].content[0].type, "text");
  EXPECT_EQ(sent[3].content[0].text, "continue");
}

TEST_F(AgentLoopTest, ExplicitLookupRequestRetriesOnDeferredPreamble) {
  auto lookup_provider = std::make_shared<LookupRetryMockProvider>();

  ravbot::AgentConfig agent_config;
  agent_config.model = "test-model";
  agent_config.temperature = 0.5;
  agent_config.max_tokens = 2048;
  agent_config.max_iterations = 15;

  ravbot::AgentLoop loop(memory_manager_, skill_loader_, tool_registry_,
                            lookup_provider, agent_config, logger_);

  auto new_msgs = loop.ProcessMessage(
      u8"我需要你帮我找github上的obsidian相关的长效记忆", {}, "System.");

  ASSERT_EQ(lookup_provider->requests.size(), 2u);
  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.size(), 1u);
  EXPECT_EQ(new_msgs.back().role, "assistant");
  EXPECT_NE(new_msgs.back().text().find("obsidian-memory"), std::string::npos);

  const auto& second_request = lookup_provider->requests[1].messages;
  ASSERT_GE(second_request.size(), 4u);
  EXPECT_EQ(second_request[0].role, "system");
  EXPECT_EQ(second_request[1].role, "user");
  EXPECT_EQ(second_request[1].text(),
            u8"我需要你帮我找github上的obsidian相关的长效记忆");
  EXPECT_EQ(second_request[2].role, "assistant");
  EXPECT_NE(second_request[2].text().find(u8"根据搜索结果"), std::string::npos);
  EXPECT_EQ(second_request[3].role, "user");
  EXPECT_NE(second_request[3].text().find("Do not stop after saying"),
            std::string::npos);
}

TEST_F(AgentLoopTest, AnthropicWebSearchReplayKeepsToolsAndAddsHint) {
  auto replay_provider = std::make_shared<WebSearchReplayMockProvider>();

  tool_registry_->RegisterExternalTool(
      "web_search", "stub web search",
      nlohmann::json::parse(
          R"JSON({"type":"object","properties":{"query":{"type":"string"}},"required":["query"]})JSON"),
      [](const nlohmann::json& params) {
        return nlohmann::json{
            {"provider", "stub"},
            {"query", params.value("query", "")},
            {"results",
             {{{"title", "example/obsidian-memory"},
               {"url", "https://github.com/example/obsidian-memory"},
               {"description",
                "Long-term memory for Obsidian-style "
                "workflows."}}}}}
            .dump();
      });

  ravbot::AgentConfig agent_config;
  agent_config.model = "test-model";
  agent_config.temperature = 0.5;
  agent_config.max_tokens = 2048;
  agent_config.max_iterations = 15;

  ravbot::AgentLoop loop(memory_manager_, skill_loader_, tool_registry_,
                            replay_provider, agent_config, logger_);

  auto new_msgs = loop.ProcessMessage(
      u8"我需要你帮我找github上的obsidian相关的长效记忆", {}, "System.");

  ASSERT_FALSE(new_msgs.empty());
  ASSERT_EQ(replay_provider->requests.size(), 2u);

  const auto& second_request = replay_provider->requests[1];
  EXPECT_GT(second_request.tools.size(), 1u);
  bool found_web_search = false;
  for (const auto& tool : second_request.tools) {
    if (tool.contains("function") && tool["function"].contains("name") &&
        tool["function"]["name"] == "web_search") {
      found_web_search = true;
      break;
    }
  }
  EXPECT_TRUE(found_web_search);

  bool has_replay_hint = false;
  for (const auto& msg : second_request.messages) {
    if (msg.role == "system" &&
        msg.text().find("[web_search replay hint]") != std::string::npos) {
      has_replay_hint = true;
      break;
    }
  }
  EXPECT_TRUE(has_replay_hint);
}

TEST_F(AgentLoopTest, WebSearchReplayRetriesRawSearchDumpResponse) {
  auto replay_provider = std::make_shared<WebSearchReplayMockProvider>();
  replay_provider->return_raw_dump_on_second = true;

  tool_registry_->RegisterExternalTool(
      "web_search", "stub web search",
      nlohmann::json::parse(
          R"JSON({"type":"object","properties":{"query":{"type":"string"}},"required":["query"]})JSON"),
      [](const nlohmann::json& params) {
        return nlohmann::json{
            {"provider", "stub"},
            {"query", params.value("query", "")},
            {"results",
             {{{"title", "example/obsidian-memory"},
               {"url", "https://github.com/example/obsidian-memory"},
               {"description",
                "Long-term memory for Obsidian-style "
                "workflows."}}}}}
            .dump();
      });

  ravbot::AgentConfig agent_config;
  agent_config.model = "test-model";
  agent_config.temperature = 0.5;
  agent_config.max_tokens = 2048;
  agent_config.max_iterations = 15;

  ravbot::AgentLoop loop(memory_manager_, skill_loader_, tool_registry_,
                            replay_provider, agent_config, logger_);

  auto new_msgs = loop.ProcessMessage(
      u8"我需要你帮我找github上的obsidian相关的长效记忆", {}, "System.");

  ASSERT_FALSE(new_msgs.empty());
  ASSERT_EQ(replay_provider->requests.size(), 3u);
  EXPECT_EQ(new_msgs.back().role, "assistant");
  EXPECT_NE(new_msgs.back().text().find("example/obsidian-memory"),
            std::string::npos);

  const auto& third_request = replay_provider->requests[2].messages;
  ASSERT_GE(third_request.size(), 5u);
  EXPECT_EQ(third_request.back().role, "user");
  EXPECT_NE(
      third_request.back().text().find("Do not perform another web search"),
      std::string::npos);
}

// --- usage_session_key routing tests ---

// Helper fixture alias for clarity
class AgentLoopUsageKeyTest : public AgentLoopTest {
 protected:
  void SetUp() override {
    AgentLoopTest::SetUp();
    accumulator_ = std::make_shared<ravbot::UsageAccumulator>();
    agent_loop_->SetUsageAccumulator(accumulator_);
    agent_loop_->SetSessionKey("internal-session");
    // Provide non-zero tokens so turns > 0 regardless of actual content
    mock_provider_->mock_usage = {10, 5, 15};
    mock_provider_->response_text = "ok";
  }

  std::shared_ptr<ravbot::UsageAccumulator> accumulator_;
};

// When usage_session_key is non-empty, usage is recorded under that key (not
// session_key_)
TEST_F(AgentLoopUsageKeyTest, UsageRecordedUnderCustomKey) {
  agent_loop_->ProcessMessage("hi", {}, "sys", "custom-key");

  EXPECT_EQ(accumulator_->GetSession("custom-key").turns, 1);
  EXPECT_EQ(accumulator_->GetSession("internal-session").turns, 0);
}

// When usage_session_key is empty, usage falls back to session_key_
TEST_F(AgentLoopUsageKeyTest, UsageFallsBackToSessionKey) {
  agent_loop_->ProcessMessage("hi", {},
                              "sys");  // usage_session_key defaults to ""

  EXPECT_EQ(accumulator_->GetSession("internal-session").turns, 1);
  EXPECT_EQ(accumulator_->GetSession("custom-key").turns, 0);
}

// When both usage_session_key and session_key_ are empty, no usage is recorded
TEST_F(AgentLoopUsageKeyTest, NeitherKeySetNoUsageRecorded) {
  agent_loop_->SetSessionKey("");
  agent_loop_->ProcessMessage("hi", {}, "sys");

  EXPECT_EQ(accumulator_->GetGlobal().turns, 0);
}

// Stream: non-empty usage_session_key routes usage to that key
TEST_F(AgentLoopUsageKeyTest, StreamUsageRecordedUnderCustomKey) {
  agent_loop_->ProcessMessageStream(
      "hi", {}, "sys", [](const ravbot::AgentEvent&) {}, "custom-key");

  EXPECT_EQ(accumulator_->GetSession("custom-key").turns, 1);
  EXPECT_EQ(accumulator_->GetSession("internal-session").turns, 0);
}

// Stream: empty usage_session_key falls back to session_key_
TEST_F(AgentLoopUsageKeyTest, StreamUsageFallsBackToSessionKey) {
  agent_loop_->ProcessMessageStream("hi", {}, "sys",
                                    [](const ravbot::AgentEvent&) {});

  EXPECT_EQ(accumulator_->GetSession("internal-session").turns, 1);
  EXPECT_EQ(accumulator_->GetSession("custom-key").turns, 0);
}
