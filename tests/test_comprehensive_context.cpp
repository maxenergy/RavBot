// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0
//
// 综合上下文和对话递归传递测试
// 专门测试本轮改进：Anthropic tool_result 续轮时的上下文裁剪

#include <filesystem>
#include <fstream>
#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "ravbot/config.hpp"
#include "ravbot/core/agent_loop.hpp"
#include "ravbot/core/memory_manager.hpp"
#include "ravbot/core/skill_loader.hpp"
#include "ravbot/providers/llm_provider.hpp"
#include "ravbot/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

namespace ravbot {

// Mock provider that captures all requests for inspection
class ContextCapturingMockProvider : public LLMProvider {
 public:
  std::vector<ChatCompletionRequest> all_requests;
  int call_count = 0;

  // 模拟工具调用场景
  bool should_call_tool_on_first = true;
  std::string tool_name = "read";
  std::string tool_result_content = "file content";

  ChatCompletionResponse
  ChatCompletion(const ChatCompletionRequest& request) override {
    all_requests.push_back(request);
    call_count++;

    ChatCompletionResponse resp;

    // 第一次调用：返回工具调用
    if (call_count == 1 && should_call_tool_on_first) {
      ToolCall tc;
      tc.id = "tool_call_1";
      tc.name = tool_name;
      tc.arguments = {{"path", "/test/file.txt"}};
      resp.content = "Let me check that file.";
      resp.tool_calls = {tc};
      resp.finish_reason = "tool_calls";
    } else {
      // 后续调用：返回最终答案
      resp.content = "Here is the answer based on the file.";
      resp.finish_reason = "stop";
    }

    resp.usage = {10, 5, 15};
    return resp;
  }

  void ChatCompletionStream(
      const ChatCompletionRequest& request,
      std::function<void(const ChatCompletionResponse&)> callback) override {
    all_requests.push_back(request);
    call_count++;

    ChatCompletionResponse resp;
    resp.content = "Streamed response";
    resp.is_stream_end = true;
    resp.usage = {10, 5, 15};
    callback(resp);
  }

  std::string GetProviderName() const override {
    return "anthropic";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"claude-3"};
  }
};

class ComprehensiveContextTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = test::MakeTestDir("ravbot_comprehensive_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    memory_manager_ = std::make_shared<MemoryManager>(test_dir_, logger_);
    skill_loader_ = std::make_shared<SkillLoader>(logger_);
    tool_registry_ = std::make_shared<ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    mock_provider_ = std::make_shared<ContextCapturingMockProvider>();

    AgentConfig agent_config;
    agent_config.model = "claude-3-sonnet";
    agent_config.temperature = 0.7;
    agent_config.max_tokens = 4096;
    agent_config.max_iterations = 10;

    agent_loop_ = std::make_unique<AgentLoop>(memory_manager_, skill_loader_,
                                              tool_registry_, mock_provider_,
                                              agent_config, logger_);
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<MemoryManager> memory_manager_;
  std::shared_ptr<SkillLoader> skill_loader_;
  std::shared_ptr<ToolRegistry> tool_registry_;
  std::shared_ptr<ContextCapturingMockProvider> mock_provider_;
  std::unique_ptr<AgentLoop> agent_loop_;
};

// ================================================================
// 测试 1: 多轮对话后工具续轮不携带旧历史
// ================================================================
TEST_F(ComprehensiveContextTest, MultiTurnToolReplayDoesNotCarryOldHistory) {
  // 构建多轮历史对话
  std::vector<Message> history;

  // 第一轮：旧问题
  history.push_back(Message{"user", "你有什么技能？"});
  history.push_back(
      Message{"assistant", "我可以帮你写代码、运行测试、搜索资料等。"});

  // 第二轮：另一个旧问题
  history.push_back(Message{"user", "有什么方法可以让你获得长效记忆？"});
  history.push_back(
      Message{"assistant", "我可以通过向量数据库和 BM25 搜索来实现长效记忆。"});

  // 第三轮：当前问题（会触发工具调用）
  auto new_msgs =
      agent_loop_->ProcessMessage("帮我详细研究一下 obsidian 的接入", history,
                                  "You are a helpful assistant.");

  // 验证：应该有两次请求（第一次工具调用，第二次工具结果续轮）
  ASSERT_EQ(mock_provider_->all_requests.size(), 2u);

  // 第一次请求：新且自包含的问题应该丢弃无关旧历史
  const auto& first_req = mock_provider_->all_requests[0];
  ASSERT_EQ(first_req.messages.size(), 2u);
  EXPECT_EQ(first_req.messages[0].role, "system");
  EXPECT_EQ(first_req.messages[1].role, "user");
  EXPECT_EQ(first_req.messages[1].text(), "帮我详细研究一下 obsidian 的接入");

  // 第二次请求（工具续轮）：应该只包含当前轮次
  const auto& second_req = mock_provider_->all_requests[1];

  // 验证：第二次请求不应该包含旧问题
  bool found_old_question_1 = false;
  bool found_old_question_2 = false;
  bool found_current_question = false;

  for (const auto& msg : second_req.messages) {
    std::string msg_text = msg.text();
    if (msg_text.find("你有什么技能") != std::string::npos) {
      found_old_question_1 = true;
    }
    if (msg_text.find("长效记忆") != std::string::npos) {
      found_old_question_2 = true;
    }
    if (msg_text.find("obsidian") != std::string::npos) {
      found_current_question = true;
    }
  }

  // 关键断言：旧问题不应该出现在工具续轮请求中
  EXPECT_FALSE(found_old_question_1)
      << "Old question 1 should not appear in tool replay";
  EXPECT_FALSE(found_old_question_2)
      << "Old question 2 should not appear in tool replay";
  EXPECT_TRUE(found_current_question)
      << "Current question should appear in tool replay";

  // 验证消息结构：system + current_user + assistant_tool_use + user_tool_result
  EXPECT_LE(second_req.messages.size(), 5u)
      << "Tool replay should only contain current turn";
}

// ================================================================
// 测试 2: 工具续轮时工具列表被正确过滤
// ================================================================
TEST_F(ComprehensiveContextTest, ToolReplayNarrowsToolList) {
  auto new_msgs = agent_loop_->ProcessMessage(
      "Read the file /test/data.txt", {}, "You are a helpful assistant.");

  ASSERT_EQ(mock_provider_->all_requests.size(), 2u);

  // 第一次请求：应该有多个工具
  const auto& first_req = mock_provider_->all_requests[0];
  EXPECT_GT(first_req.tools.size(), 1u)
      << "First request should have multiple tools";

  // 第二次请求（工具续轮）：应该只有匹配的工具
  const auto& second_req = mock_provider_->all_requests[1];
  EXPECT_EQ(second_req.tools.size(), 1u)
      << "Tool replay should narrow to matched tool only";

  // 验证工具名称匹配
  if (!second_req.tools.empty() && second_req.tools[0].contains("function")) {
    EXPECT_EQ(second_req.tools[0]["function"]["name"], "read");
  }
}

// ================================================================
// 测试 3: 连续多次工具调用的上下文传递
// ================================================================
TEST_F(ComprehensiveContextTest, ConsecutiveToolCallsContextPropagation) {
  // 模拟需要多次工具调用的场景
  mock_provider_->should_call_tool_on_first = true;

  std::vector<Message> history;
  history.push_back(Message{"user", "First question"});
  history.push_back(Message{"assistant", "First answer"});

  auto new_msgs = agent_loop_->ProcessMessage("Second question requiring tool",
                                              history, "System prompt");

  // 验证每次请求的上下文都是正确的
  for (size_t i = 0; i < mock_provider_->all_requests.size(); ++i) {
    const auto& req = mock_provider_->all_requests[i];

    // 所有请求都应该有 system prompt
    bool has_system = false;
    for (const auto& msg : req.messages) {
      if (msg.role == "system") {
        has_system = true;
        break;
      }
    }
    EXPECT_TRUE(has_system) << "Request " << i << " should have system prompt";
  }
}

// ================================================================
// 测试 4: 空历史的工具调用
// ================================================================
TEST_F(ComprehensiveContextTest, ToolCallWithEmptyHistory) {
  auto new_msgs =
      agent_loop_->ProcessMessage("Read a file", {}, "You are helpful.");

  ASSERT_GE(mock_provider_->all_requests.size(), 1u);

  // 即使没有历史，工具续轮也应该正常工作
  if (mock_provider_->all_requests.size() >= 2) {
    const auto& second_req = mock_provider_->all_requests[1];

    // 应该只包含当前轮次
    EXPECT_LE(second_req.messages.size(), 5u);

    // 应该有 tool_result
    bool has_tool_result = false;
    for (const auto& msg : second_req.messages) {
      for (const auto& block : msg.content) {
        if (block.type == "tool_result") {
          has_tool_result = true;
          break;
        }
      }
    }
    EXPECT_TRUE(has_tool_result);
  }
}

// ================================================================
// 测试 5: 流式首轮请求也应聚焦当前问题
// ================================================================
TEST_F(ComprehensiveContextTest, StreamingNewQuestionDoesNotCarryOldHistory) {
  std::vector<Message> history = {
      Message{"user", "你有什么技能？"},
      Message{"assistant", "我可以帮你写代码、运行测试、搜索资料等。"},
      Message{"user", "有什么方法可以让你获得长效记忆？"},
      Message{"assistant", "我可以通过向量数据库和 BM25 搜索来实现长效记忆。"},
  };

  auto streamed = agent_loop_->ProcessMessageStream(
      "帮我找一下 github 上和 obsidian 接入相关的项目", history,
      "You are a helpful assistant.", [](const AgentEvent&) {});

  ASSERT_FALSE(streamed.empty());
  ASSERT_EQ(mock_provider_->all_requests.size(), 1u);

  const auto& first_req = mock_provider_->all_requests[0];
  ASSERT_EQ(first_req.messages.size(), 2u);
  EXPECT_EQ(first_req.messages[0].role, "system");
  EXPECT_EQ(first_req.messages[1].role, "user");
  EXPECT_EQ(first_req.messages[1].text(),
            "帮我找一下 github 上和 obsidian 接入相关的项目");
}

// ================================================================
// 测试 5: 流式响应的上下文处理
// ================================================================
TEST_F(ComprehensiveContextTest, StreamingWithContextManagement) {
  std::vector<Message> history;
  history.push_back(Message{"user", "Previous context"});
  history.push_back(Message{"assistant", "Previous response"});

  std::vector<AgentEvent> events;
  auto new_msgs = agent_loop_->ProcessMessageStream(
      "New question", history, "System",
      [&events](const AgentEvent& event) { events.push_back(event); });

  // 验证流式请求也正确处理了上下文
  ASSERT_GE(mock_provider_->all_requests.size(), 1u);
  const auto& req = mock_provider_->all_requests[0];

  // 新且自包含的问题应只带 system + 当前 user
  EXPECT_EQ(req.messages.size(), 2u);
  EXPECT_TRUE(req.stream);
}

}  // namespace ravbot
