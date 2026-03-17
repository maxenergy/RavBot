// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/security/audit_logger.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <spdlog/sinks/null_sink.h>

namespace ravbot {

class AuditLoggerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 创建空的 logger
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    // 创建测试日志目录
    test_log_dir_ = "/tmp/test_audit_logs";
    std::filesystem::create_directories(test_log_dir_);

    audit_logger_ = std::make_unique<SecurityAuditLogger>(logger_);
    audit_logger_->SetLogDirectory(test_log_dir_);
  }

  void TearDown() override {
    // 清理测试日志目录
    if (std::filesystem::exists(test_log_dir_)) {
      std::filesystem::remove_all(test_log_dir_);
    }
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<SecurityAuditLogger> audit_logger_;
  std::string test_log_dir_;
};

// 测试外部内容包装日志
// Requirements: 15.1
TEST_F(AuditLoggerTest, LogContentWrapping) {
  audit_logger_->LogContentWrapping(
      "user123", "session456", "web_search",
      "https://example.com", 1024);

  // 验证日志文件存在
  std::string log_file = audit_logger_->GetCurrentLogPath();
  EXPECT_TRUE(std::filesystem::exists(log_file));

  // 读取并验证日志内容
  std::ifstream file(log_file);
  std::string line;
  ASSERT_TRUE(std::getline(file, line));

  nlohmann::json j = nlohmann::json::parse(line);
  EXPECT_EQ(j["event_type"], "content_wrapping");
  EXPECT_EQ(j["user_id"], "user123");
  EXPECT_EQ(j["session_id"], "session456");
  EXPECT_EQ(j["details"]["tool_name"], "web_search");
  EXPECT_EQ(j["details"]["url"], "https://example.com");
  EXPECT_EQ(j["details"]["content_size"], 1024);
  EXPECT_EQ(j["severity"], "info");
}

// 测试边界标记清理日志
// Requirements: 15.2
TEST_F(AuditLoggerTest, LogMarkerSanitization) {
  audit_logger_->LogMarkerSanitization(
      "user123", "session456", "gateway", 3);

  std::string log_file = audit_logger_->GetCurrentLogPath();
  std::ifstream file(log_file);
  std::string line;
  ASSERT_TRUE(std::getline(file, line));

  nlohmann::json j = nlohmann::json::parse(line);
  EXPECT_EQ(j["event_type"], "marker_sanitization");
  EXPECT_EQ(j["user_id"], "user123");
  EXPECT_EQ(j["details"]["markers_removed"], 3);
  EXPECT_EQ(j["severity"], "warning");  // markers_removed > 0
}

// 测试危险工具调用日志
// Requirements: 15.3
TEST_F(AuditLoggerTest, LogDangerousToolCall) {
  nlohmann::json params = {{"command", "rm -rf /"}};

  audit_logger_->LogDangerousToolCall(
      "user123", "session456", "bash", params, false);

  std::string log_file = audit_logger_->GetCurrentLogPath();
  std::ifstream file(log_file);
  std::string line;
  ASSERT_TRUE(std::getline(file, line));

  nlohmann::json j = nlohmann::json::parse(line);
  EXPECT_EQ(j["event_type"], "dangerous_tool_call");
  EXPECT_EQ(j["details"]["tool_name"], "bash");
  EXPECT_EQ(j["details"]["approved"], false);
  EXPECT_EQ(j["severity"], "error");  // not approved
}

// 测试认证失败日志
// Requirements: 15.4
TEST_F(AuditLoggerTest, LogAuthFailure) {
  audit_logger_->LogAuthFailure(
      "user123", "telegram", "invalid_token");

  std::string log_file = audit_logger_->GetCurrentLogPath();
  std::ifstream file(log_file);
  std::string line;
  ASSERT_TRUE(std::getline(file, line));

  nlohmann::json j = nlohmann::json::parse(line);
  EXPECT_EQ(j["event_type"], "auth_failure");
  EXPECT_EQ(j["user_id"], "user123");
  EXPECT_EQ(j["source"], "telegram");
  EXPECT_EQ(j["details"]["reason"], "invalid_token");
  EXPECT_EQ(j["severity"], "error");
}

// 测试策略变更日志
// Requirements: 15.5
TEST_F(AuditLoggerTest, LogPolicyChange) {
  nlohmann::json old_value = {{"allow_dangerous_tools", true}};
  nlohmann::json new_value = {{"allow_dangerous_tools", false}};

  audit_logger_->LogPolicyChange(
      "admin", "security_policy", old_value, new_value);

  std::string log_file = audit_logger_->GetCurrentLogPath();
  std::ifstream file(log_file);
  std::string line;
  ASSERT_TRUE(std::getline(file, line));

  nlohmann::json j = nlohmann::json::parse(line);
  EXPECT_EQ(j["event_type"], "policy_change");
  EXPECT_EQ(j["user_id"], "admin");
  EXPECT_EQ(j["details"]["policy_name"], "security_policy");
  EXPECT_EQ(j["severity"], "warning");
}

// 测试访问拒绝日志
TEST_F(AuditLoggerTest, LogAccessDenied) {
  audit_logger_->LogAccessDenied(
      "user123", "session456", "/admin/config", "insufficient_permissions");

  std::string log_file = audit_logger_->GetCurrentLogPath();
  std::ifstream file(log_file);
  std::string line;
  ASSERT_TRUE(std::getline(file, line));

  nlohmann::json j = nlohmann::json::parse(line);
  EXPECT_EQ(j["event_type"], "access_denied");
  EXPECT_EQ(j["details"]["resource"], "/admin/config");
  EXPECT_EQ(j["details"]["reason"], "insufficient_permissions");
  EXPECT_EQ(j["severity"], "warning");
}

// 测试可疑活动日志
TEST_F(AuditLoggerTest, LogSuspiciousActivity) {
  nlohmann::json evidence = {
      {"attempts", 10},
      {"time_window", "60s"}
  };

  audit_logger_->LogSuspiciousActivity(
      "user123", "session456",
      "Multiple failed login attempts", evidence);

  std::string log_file = audit_logger_->GetCurrentLogPath();
  std::ifstream file(log_file);
  std::string line;
  ASSERT_TRUE(std::getline(file, line));

  nlohmann::json j = nlohmann::json::parse(line);
  EXPECT_EQ(j["event_type"], "suspicious_activity");
  EXPECT_EQ(j["details"]["description"], "Multiple failed login attempts");
  EXPECT_EQ(j["details"]["evidence"]["attempts"], 10);
  EXPECT_EQ(j["severity"], "critical");
}

// 测试多条日志记录
TEST_F(AuditLoggerTest, MultipleLogEntries) {
  audit_logger_->LogContentWrapping("user1", "s1", "tool1", "url1", 100);
  audit_logger_->LogContentWrapping("user2", "s2", "tool2", "url2", 200);
  audit_logger_->LogAuthFailure("user3", "source", "reason");

  std::string log_file = audit_logger_->GetCurrentLogPath();
  std::ifstream file(log_file);

  int line_count = 0;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty()) {
      line_count++;
    }
  }

  EXPECT_EQ(line_count, 3);
}

// 测试导出功能
// Requirements: 15.6
TEST_F(AuditLoggerTest, Export) {
  auto start_time = std::chrono::system_clock::now() - std::chrono::hours(1);

  audit_logger_->LogContentWrapping("user1", "s1", "tool1", "url1", 100);
  audit_logger_->LogContentWrapping("user2", "s2", "tool2", "url2", 200);
  audit_logger_->LogAuthFailure("user1", "source", "reason");

  auto end_time = std::chrono::system_clock::now() + std::chrono::hours(1);

  // 导出所有日志
  auto all_entries = audit_logger_->Export(start_time, end_time);
  EXPECT_EQ(all_entries.size(), 3);

  // 导出特定用户的日志
  auto user1_entries = audit_logger_->Export(start_time, end_time, "user1");
  EXPECT_EQ(user1_entries.size(), 2);
}

// 测试查询功能
// Requirements: 15.7
TEST_F(AuditLoggerTest, Query) {
  auto start_time = std::chrono::system_clock::now() - std::chrono::hours(1);

  audit_logger_->LogContentWrapping("user1", "s1", "tool1", "url1", 100);
  audit_logger_->LogContentWrapping("user2", "s2", "tool2", "url2", 200);
  audit_logger_->LogAuthFailure("user1", "source", "reason");

  auto end_time = std::chrono::system_clock::now() + std::chrono::hours(1);

  // 查询特定事件类型
  auto wrapping_entries = audit_logger_->Query(
      AuditEventType::kContentWrapping, start_time, end_time);
  EXPECT_EQ(wrapping_entries.size(), 2);

  auto auth_entries = audit_logger_->Query(
      AuditEventType::kAuthFailure, start_time, end_time);
  EXPECT_EQ(auth_entries.size(), 1);
}

// 测试 AuditEntry 序列化
TEST_F(AuditLoggerTest, AuditEntrySerialization) {
  AuditEntry entry;
  entry.id = "test123";
  entry.event_type = AuditEventType::kContentWrapping;
  entry.timestamp = std::chrono::system_clock::now();
  entry.user_id = "user123";
  entry.session_id = "session456";
  entry.source = "test_source";
  entry.action = "test_action";
  entry.details = {{"key", "value"}};
  entry.severity = "info";

  nlohmann::json j = entry.ToJson();
  EXPECT_EQ(j["id"], "test123");
  EXPECT_EQ(j["event_type"], "content_wrapping");
  EXPECT_EQ(j["user_id"], "user123");
  EXPECT_EQ(j["session_id"], "session456");
  EXPECT_EQ(j["source"], "test_source");
  EXPECT_EQ(j["action"], "test_action");
  EXPECT_EQ(j["details"]["key"], "value");
  EXPECT_EQ(j["severity"], "info");

  AuditEntry deserialized = AuditEntry::FromJson(j);
  EXPECT_EQ(deserialized.id, entry.id);
  EXPECT_EQ(deserialized.event_type, entry.event_type);
  EXPECT_EQ(deserialized.user_id, entry.user_id);
  EXPECT_EQ(deserialized.session_id, entry.session_id);
  EXPECT_EQ(deserialized.source, entry.source);
  EXPECT_EQ(deserialized.action, entry.action);
  EXPECT_EQ(deserialized.severity, entry.severity);
}

// 测试事件类型字符串转换
TEST_F(AuditLoggerTest, EventTypeStringConversion) {
  EXPECT_EQ(AuditEventTypeToString(AuditEventType::kContentWrapping),
            "content_wrapping");
  EXPECT_EQ(AuditEventTypeToString(AuditEventType::kMarkerSanitization),
            "marker_sanitization");
  EXPECT_EQ(AuditEventTypeToString(AuditEventType::kDangerousToolCall),
            "dangerous_tool_call");
  EXPECT_EQ(AuditEventTypeToString(AuditEventType::kAuthFailure),
            "auth_failure");
  EXPECT_EQ(AuditEventTypeToString(AuditEventType::kPolicyChange),
            "policy_change");

  EXPECT_EQ(StringToAuditEventType("content_wrapping"),
            AuditEventType::kContentWrapping);
  EXPECT_EQ(StringToAuditEventType("marker_sanitization"),
            AuditEventType::kMarkerSanitization);
  EXPECT_EQ(StringToAuditEventType("dangerous_tool_call"),
            AuditEventType::kDangerousToolCall);
  EXPECT_EQ(StringToAuditEventType("auth_failure"),
            AuditEventType::kAuthFailure);
  EXPECT_EQ(StringToAuditEventType("policy_change"),
            AuditEventType::kPolicyChange);
}

// 测试日志目录设置
TEST_F(AuditLoggerTest, SetLogDirectory) {
  std::string custom_dir = "/tmp/custom_audit_logs";
  audit_logger_->SetLogDirectory(custom_dir);

  audit_logger_->LogContentWrapping("user1", "s1", "tool1", "url1", 100);

  // 验证日志文件在自定义目录中
  std::string log_file = audit_logger_->GetCurrentLogPath();
  EXPECT_TRUE(log_file.find(custom_dir) != std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(log_file));

  // 清理
  std::filesystem::remove_all(custom_dir);
}

}  // namespace ravbot
