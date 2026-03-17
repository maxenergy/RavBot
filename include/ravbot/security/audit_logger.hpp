// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace ravbot {

// 审计事件类型
// Requirements: 15.1, 15.2, 15.3, 15.4, 15.5
enum class AuditEventType {
  kContentWrapping,       // 外部内容包装
  kMarkerSanitization,    // 边界标记清理
  kDangerousToolCall,     // 危险工具调用
  kAuthFailure,           // 认证失败
  kPolicyChange,          // 策略变更
  kAccessDenied,          // 访问拒绝
  kSuspiciousActivity     // 可疑活动
};

// 审计条目
// Requirements: 15.1-15.8
struct AuditEntry {
  std::string id;                                    // 唯一标识符
  AuditEventType event_type;                         // 事件类型
  std::chrono::system_clock::time_point timestamp;   // 时间戳
  std::string user_id;                               // 用户 ID
  std::string session_id;                            // 会话 ID
  std::string source;                                // 事件来源
  std::string action;                                // 执行的操作
  nlohmann::json details;                            // 详细信息
  std::string severity;                              // 严重程度（info, warning, error, critical）

  nlohmann::json ToJson() const;
  static AuditEntry FromJson(const nlohmann::json& j);
};

// 安全审计日志记录器
// Requirements: 15.1-15.8
class SecurityAuditLogger {
 public:
  explicit SecurityAuditLogger(std::shared_ptr<spdlog::logger> logger);

  // 记录外部内容包装事件
  // Requirements: 15.1
  void LogContentWrapping(const std::string& user_id,
                          const std::string& session_id,
                          const std::string& tool_name,
                          const std::string& url,
                          size_t content_size);

  // 记录边界标记清理事件
  // Requirements: 15.2
  void LogMarkerSanitization(const std::string& user_id,
                             const std::string& session_id,
                             const std::string& source,
                             int markers_removed);

  // 记录危险工具调用事件
  // Requirements: 15.3
  void LogDangerousToolCall(const std::string& user_id,
                            const std::string& session_id,
                            const std::string& tool_name,
                            const nlohmann::json& parameters,
                            bool approved);

  // 记录认证失败事件
  // Requirements: 15.4
  void LogAuthFailure(const std::string& user_id,
                      const std::string& source,
                      const std::string& reason);

  // 记录策略变更事件
  // Requirements: 15.5
  void LogPolicyChange(const std::string& user_id,
                       const std::string& policy_name,
                       const nlohmann::json& old_value,
                       const nlohmann::json& new_value);

  // 记录访问拒绝事件
  void LogAccessDenied(const std::string& user_id,
                       const std::string& session_id,
                       const std::string& resource,
                       const std::string& reason);

  // 记录可疑活动事件
  void LogSuspiciousActivity(const std::string& user_id,
                             const std::string& session_id,
                             const std::string& description,
                             const nlohmann::json& evidence);

  // 导出审计日志
  // Requirements: 15.6
  std::vector<AuditEntry> Export(
      const std::chrono::system_clock::time_point& start_time,
      const std::chrono::system_clock::time_point& end_time,
      const std::string& filter_user_id = "") const;

  // 查询审计日志
  // Requirements: 15.7
  std::vector<AuditEntry> Query(
      AuditEventType event_type,
      const std::chrono::system_clock::time_point& start_time,
      const std::chrono::system_clock::time_point& end_time) const;

  // 设置日志目录
  void SetLogDirectory(const std::string& directory);

  // 获取当前日志文件路径
  std::string GetCurrentLogPath() const;

 private:
  // 记录审计条目
  void LogEntry(const AuditEntry& entry);

  // 生成唯一 ID
  std::string GenerateId() const;

  // 获取当前日志文件路径（按日期）
  std::string GetLogFilePath() const;

  // 读取日志文件
  std::vector<AuditEntry> ReadLogFile(const std::string& file_path) const;

  std::shared_ptr<spdlog::logger> logger_;
  std::string log_directory_;
};

// 辅助函数：将 AuditEventType 转换为字符串
std::string AuditEventTypeToString(AuditEventType type);

// 辅助函数：将字符串转换为 AuditEventType
AuditEventType StringToAuditEventType(const std::string& str);

}  // namespace ravbot
