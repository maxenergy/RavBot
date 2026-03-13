// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/tools/tool_registry.hpp"

#include <gtest/gtest.h>

using namespace quantclaw;

// Mock provider that captures all requests
class CapturingMockProvider : public LLMProvider {
 public:
  std::vector<ChatCompletionRequest> requests;
  int call_count = 0;

  std::string GetProviderName() const override {
    return "anthropic";
  }

  std::vector<std::string> GetSupportedModels() const override {
    return {"test-model"};
  }

  ChatCompletionResponse
  ChatCompletion(const ChatCompletionRequest& request) override {
    requests.push_back(request);
    call_count++;

    ChatCompletionResponse response;

    // First call: return tool call
    if (call_count == 1) {
      response.content = "I'll search for that.";

      // Simulate a web_search tool call
      ToolCall tc;
      tc.id = "tooluse_search123";
      tc.name = "web_search";
      tc.arguments =
          nlohmann::json{{"query", "obsidian long-term memory github"}};
      response.tool_calls.push_back(tc);
    } else {
      // Second call: return final response after tool execution
      response.content =
          "Here are the search results for Obsidian long-term memory on "
          "GitHub.";
    }

    return response;
  }

  void ChatCompletionStream(
      const ChatCompletionRequest&,
      std::function<void(const ChatCompletionResponse&)>) override {}
};

class ContextPollutionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    // Create a temporary workspace directory
    workspace_path_ =
        std::filesystem::temp_directory_path() / "quantclaw_test_workspace";
    std::filesystem::create_directories(workspace_path_);

    memory_manager_ = std::make_shared<MemoryManager>(workspace_path_, logger_);
    skill_loader_ = std::make_shared<SkillLoader>(logger_);
    tool_registry_ = std::make_shared<ToolRegistry>(logger_);
  }

  void TearDown() override {
    // Clean up temporary workspace
    if (std::filesystem::exists(workspace_path_)) {
      std::filesystem::remove_all(workspace_path_);
    }
  }

  std::filesystem::path workspace_path_;
  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<MemoryManager> memory_manager_;
  std::shared_ptr<SkillLoader> skill_loader_;
  std::shared_ptr<ToolRegistry> tool_registry_;
};

TEST_F(ContextPollutionTest,
       NewQuestionShouldNotIncludeOldConversationHistory) {
  auto provider = std::make_shared<CapturingMockProvider>();

  AgentConfig config;
  config.model = "test-model";
  config.temperature = 0.5;
  config.max_tokens = 2048;
  config.max_iterations = 2;
  config.auto_compact =
      false;  // Disable auto-compaction to test pure context trimming

  AgentLoop loop(memory_manager_, skill_loader_, tool_registry_, provider,
                 config, logger_);

  // Simulate old conversation history
  std::vector<Message> history = {
      Message{"user", "你有什么技能？"},
      Message{"assistant", "我可以帮助你写代码、运行测试和搜索资料。"},
      Message{"user", "有什么方法可以让你获得长效记忆以及快速检索？"},
      Message{"assistant", "我可以通过结构化记忆系统来增强长期记忆。"},
  };

  // Ask a NEW question (not related to old history)
  auto new_msgs =
      loop.ProcessMessage("我需要你帮我找github上的obsidian相关的长效记忆",
                          history, "You are a helpful assistant.");

  ASSERT_FALSE(new_msgs.empty());
  ASSERT_GE(provider->requests.size(), 1u);

  // Check the FIRST request (before tool execution)
  const auto& first_request = provider->requests[0];

  // A self-contained new question should start fresh:
  // 1. System prompt
  // 2. New user question
  EXPECT_EQ(first_request.messages.size(), 2u);
  EXPECT_EQ(first_request.messages[0].role, "system");
  EXPECT_EQ(first_request.messages[1].role, "user");
  EXPECT_EQ(first_request.messages[1].text(),
            "我需要你帮我找github上的obsidian相关的长效记忆");

  // Verify the tool call uses the CORRECT query (from new question)
  ASSERT_FALSE(new_msgs.empty());
  bool found_correct_tool_call = false;
  for (const auto& msg : new_msgs) {
    for (const auto& block : msg.content) {
      if (block.type == "tool_use" && block.name == "web_search") {
        auto query = block.input.value("query", "");
        // Should contain "obsidian" or "github", NOT "技能"
        EXPECT_TRUE(query.find("obsidian") != std::string::npos ||
                    query.find("github") != std::string::npos);
        EXPECT_TRUE(query.find("你有什么技能") == std::string::npos);
        found_correct_tool_call = true;
      }
    }
  }
  EXPECT_TRUE(found_correct_tool_call);
}

TEST_F(ContextPollutionTest, FollowUpKeepsOnlyMostRecentRelevantTurn) {
  auto provider = std::make_shared<CapturingMockProvider>();

  AgentConfig config;
  config.model = "test-model";
  config.temperature = 0.5;
  config.max_tokens = 2048;
  config.max_iterations = 2;
  config.auto_compact = false;

  AgentLoop loop(memory_manager_, skill_loader_, tool_registry_, provider,
                 config, logger_);

  std::vector<Message> history = {
      Message{"user", "你有什么技能？"},
      Message{"assistant", "我可以帮助你写代码、运行测试和搜索资料。"},
      Message{"user", "帮我详细研究一下obsidian的接入"},
      Message{"assistant", "我已经整理了 Obsidian 接入的基础方案。"},
  };

  auto new_msgs = loop.ProcessMessage("继续展开一下接入细节", history,
                                      "You are a helpful assistant.");

  ASSERT_FALSE(new_msgs.empty());
  ASSERT_GE(provider->requests.size(), 1u);

  const auto& first_request = provider->requests[0];
  ASSERT_EQ(first_request.messages.size(), 4u);

  EXPECT_EQ(first_request.messages[0].role, "system");
  EXPECT_EQ(first_request.messages[1].role, "user");
  EXPECT_EQ(first_request.messages[1].text(), "帮我详细研究一下obsidian的接入");
  EXPECT_EQ(first_request.messages[2].role, "assistant");
  EXPECT_EQ(first_request.messages[3].role, "user");
  EXPECT_EQ(first_request.messages[3].text(), "继续展开一下接入细节");

  for (const auto& msg : first_request.messages) {
    EXPECT_EQ(msg.text().find("你有什么技能"), std::string::npos);
  }
}
