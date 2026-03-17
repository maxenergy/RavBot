// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/security/trust_model.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <thread>

namespace ravbot {

class TrustModelTest : public ::testing::Test {
 protected:
  TrustModelManager manager_;
  std::string test_config_path_ = "/tmp/test_trust_model.json";

  void TearDown() override {
    if (std::filesystem::exists(test_config_path_)) {
      std::filesystem::remove(test_config_path_);
    }
  }
};

// 测试默认来源类型信任级别
// Requirements: 14.2, 14.3, 14.4, 14.5
TEST_F(TrustModelTest, DefaultSourceTrustLevels) {
  EXPECT_EQ(manager_.GetTrustLevel("user_input"), TrustLevel::kSemiTrusted);
  EXPECT_EQ(manager_.GetTrustLevel("web_search"), TrustLevel::kUntrusted);
  EXPECT_EQ(manager_.GetTrustLevel("web_fetch"), TrustLevel::kUntrusted);
  EXPECT_EQ(manager_.GetTrustLevel("local_file"), TrustLevel::kTrusted);
  EXPECT_EQ(manager_.GetTrustLevel("system"), TrustLevel::kTrusted);
  EXPECT_EQ(manager_.GetTrustLevel("channel_message"), TrustLevel::kSemiTrusted);
}

// 测试未知来源类型默认为不可信
// Requirements: 14.1
TEST_F(TrustModelTest, UnknownSourceDefaultsToUntrusted) {
  EXPECT_EQ(manager_.GetTrustLevel("unknown_source"), TrustLevel::kUntrusted);
}

// 测试工具信任级别设置和获取
// Requirements: 14.6
TEST_F(TrustModelTest, ToolTrustLevel) {
  manager_.SetToolTrustLevel("bash", TrustLevel::kTrusted);
  manager_.SetToolTrustLevel("web_search", TrustLevel::kUntrusted);

  EXPECT_EQ(manager_.GetToolTrustLevel("bash"), TrustLevel::kTrusted);
  EXPECT_EQ(manager_.GetToolTrustLevel("web_search"), TrustLevel::kUntrusted);

  // 未设置的工具默认为半可信
  EXPECT_EQ(manager_.GetToolTrustLevel("unknown_tool"), TrustLevel::kSemiTrusted);
}

// 测试发送者信任级别设置和获取
// Requirements: 14.7
TEST_F(TrustModelTest, SenderTrustLevel) {
  manager_.SetSenderTrustLevel("user123", TrustLevel::kTrusted);
  manager_.SetSenderTrustLevel("bot456", TrustLevel::kUntrusted);

  EXPECT_EQ(manager_.GetSenderTrustLevel("user123"), TrustLevel::kTrusted);
  EXPECT_EQ(manager_.GetSenderTrustLevel("bot456"), TrustLevel::kUntrusted);

  // 未设置的发送者默认为半可信
  EXPECT_EQ(manager_.GetSenderTrustLevel("unknown_sender"), TrustLevel::kSemiTrusted);
}

// 测试默认安全策略
// Requirements: 14.6
TEST_F(TrustModelTest, DefaultSecurityPolicies) {
  // Trusted 策略
  auto trusted_policy = manager_.GetPolicy(TrustLevel::kTrusted);
  EXPECT_TRUE(trusted_policy.allow_dangerous_tools);
  EXPECT_FALSE(trusted_policy.require_approval);
  EXPECT_FALSE(trusted_policy.enable_sandboxing);
  EXPECT_EQ(trusted_policy.max_content_size, 0);
  EXPECT_FALSE(trusted_policy.log_all_actions);

  // SemiTrusted 策略
  auto semi_trusted_policy = manager_.GetPolicy(TrustLevel::kSemiTrusted);
  EXPECT_TRUE(semi_trusted_policy.allow_dangerous_tools);
  EXPECT_TRUE(semi_trusted_policy.require_approval);
  EXPECT_FALSE(semi_trusted_policy.enable_sandboxing);
  EXPECT_EQ(semi_trusted_policy.max_content_size, 10 * 1024 * 1024);
  EXPECT_TRUE(semi_trusted_policy.log_all_actions);

  // Untrusted 策略
  auto untrusted_policy = manager_.GetPolicy(TrustLevel::kUntrusted);
  EXPECT_FALSE(untrusted_policy.allow_dangerous_tools);
  EXPECT_TRUE(untrusted_policy.require_approval);
  EXPECT_TRUE(untrusted_policy.enable_sandboxing);
  EXPECT_EQ(untrusted_policy.max_content_size, 1 * 1024 * 1024);
  EXPECT_TRUE(untrusted_policy.log_all_actions);
}

// 测试自定义安全策略
// Requirements: 14.6
TEST_F(TrustModelTest, CustomSecurityPolicy) {
  SecurityPolicy custom_policy;
  custom_policy.allow_dangerous_tools = false;
  custom_policy.require_approval = true;
  custom_policy.enable_sandboxing = true;
  custom_policy.max_content_size = 5 * 1024 * 1024;
  custom_policy.log_all_actions = true;

  manager_.SetPolicy(TrustLevel::kSemiTrusted, custom_policy);

  auto retrieved_policy = manager_.GetPolicy(TrustLevel::kSemiTrusted);
  EXPECT_FALSE(retrieved_policy.allow_dangerous_tools);
  EXPECT_TRUE(retrieved_policy.require_approval);
  EXPECT_TRUE(retrieved_policy.enable_sandboxing);
  EXPECT_EQ(retrieved_policy.max_content_size, 5 * 1024 * 1024);
  EXPECT_TRUE(retrieved_policy.log_all_actions);
}

// 测试配置持久化
// Requirements: 14.8
TEST_F(TrustModelTest, SaveAndLoad) {
  // 设置一些自定义配置
  manager_.SetToolTrustLevel("bash", TrustLevel::kTrusted);
  manager_.SetToolTrustLevel("web_search", TrustLevel::kUntrusted);
  manager_.SetSenderTrustLevel("user123", TrustLevel::kTrusted);

  SecurityPolicy custom_policy;
  custom_policy.allow_dangerous_tools = false;
  custom_policy.max_content_size = 2 * 1024 * 1024;
  manager_.SetPolicy(TrustLevel::kSemiTrusted, custom_policy);

  // 保存配置
  manager_.Save(test_config_path_);
  EXPECT_TRUE(std::filesystem::exists(test_config_path_));

  // 创建新的管理器并加载配置
  TrustModelManager new_manager;
  new_manager.Load(test_config_path_);

  // 验证配置被正确加载
  EXPECT_EQ(new_manager.GetToolTrustLevel("bash"), TrustLevel::kTrusted);
  EXPECT_EQ(new_manager.GetToolTrustLevel("web_search"), TrustLevel::kUntrusted);
  EXPECT_EQ(new_manager.GetSenderTrustLevel("user123"), TrustLevel::kTrusted);

  auto loaded_policy = new_manager.GetPolicy(TrustLevel::kSemiTrusted);
  EXPECT_FALSE(loaded_policy.allow_dangerous_tools);
  EXPECT_EQ(loaded_policy.max_content_size, 2 * 1024 * 1024);
}

// 测试重置功能
TEST_F(TrustModelTest, Reset) {
  // 设置一些自定义配置
  manager_.SetToolTrustLevel("bash", TrustLevel::kTrusted);
  manager_.SetSenderTrustLevel("user123", TrustLevel::kTrusted);

  // 重置
  manager_.Reset();

  // 验证配置被重置为默认值
  EXPECT_EQ(manager_.GetToolTrustLevel("bash"), TrustLevel::kSemiTrusted);
  EXPECT_EQ(manager_.GetSenderTrustLevel("user123"), TrustLevel::kSemiTrusted);

  // 验证默认策略仍然存在
  auto untrusted_policy = manager_.GetPolicy(TrustLevel::kUntrusted);
  EXPECT_FALSE(untrusted_policy.allow_dangerous_tools);
  EXPECT_TRUE(untrusted_policy.enable_sandboxing);
}

// 测试 SecurityPolicy 序列化
TEST_F(TrustModelTest, SecurityPolicySerialization) {
  SecurityPolicy policy;
  policy.allow_dangerous_tools = true;
  policy.require_approval = false;
  policy.enable_sandboxing = true;
  policy.max_content_size = 1024;
  policy.log_all_actions = true;

  nlohmann::json j = policy.ToJson();
  EXPECT_EQ(j["allow_dangerous_tools"], true);
  EXPECT_EQ(j["require_approval"], false);
  EXPECT_EQ(j["enable_sandboxing"], true);
  EXPECT_EQ(j["max_content_size"], 1024);
  EXPECT_EQ(j["log_all_actions"], true);

  SecurityPolicy deserialized = SecurityPolicy::FromJson(j);
  EXPECT_EQ(deserialized.allow_dangerous_tools, policy.allow_dangerous_tools);
  EXPECT_EQ(deserialized.require_approval, policy.require_approval);
  EXPECT_EQ(deserialized.enable_sandboxing, policy.enable_sandboxing);
  EXPECT_EQ(deserialized.max_content_size, policy.max_content_size);
  EXPECT_EQ(deserialized.log_all_actions, policy.log_all_actions);
}

// 测试 TrustLevel 字符串转换
TEST_F(TrustModelTest, TrustLevelStringConversion) {
  EXPECT_EQ(TrustLevelToString(TrustLevel::kTrusted), "trusted");
  EXPECT_EQ(TrustLevelToString(TrustLevel::kSemiTrusted), "semi_trusted");
  EXPECT_EQ(TrustLevelToString(TrustLevel::kUntrusted), "untrusted");

  EXPECT_EQ(StringToTrustLevel("trusted"), TrustLevel::kTrusted);
  EXPECT_EQ(StringToTrustLevel("semi_trusted"), TrustLevel::kSemiTrusted);
  EXPECT_EQ(StringToTrustLevel("untrusted"), TrustLevel::kUntrusted);

  // 测试无效字符串
  EXPECT_THROW(StringToTrustLevel("invalid"), std::invalid_argument);
}

// 测试并发访问
TEST_F(TrustModelTest, ConcurrentAccess) {
  std::vector<std::thread> threads;

  // 启动多个线程同时读写
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([this, i]() {
      std::string tool_name = "tool_" + std::to_string(i);
      manager_.SetToolTrustLevel(tool_name, TrustLevel::kTrusted);
      EXPECT_EQ(manager_.GetToolTrustLevel(tool_name), TrustLevel::kTrusted);
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // 验证所有工具都被正确设置
  for (int i = 0; i < 10; ++i) {
    std::string tool_name = "tool_" + std::to_string(i);
    EXPECT_EQ(manager_.GetToolTrustLevel(tool_name), TrustLevel::kTrusted);
  }
}

}  // namespace ravbot
