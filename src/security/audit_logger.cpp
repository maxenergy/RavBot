// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/security/audit_logger.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <random>

namespace ravbot {

// AuditEntry 序列化
nlohmann::json AuditEntry::ToJson() const {
  auto time_t_val = std::chrono::system_clock::to_time_t(timestamp);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%SZ");

  return {
      {"id", id},
      {"event_type", AuditEventTypeToString(event_type)},
      {"timestamp", oss.str()},
      {"user_id", user_id},
      {"session_id", session_id},
      {"source", source},
      {"action", action},
      {"details", details},
      {"severity", severity}
  };
}

// AuditEntry 反序列化
AuditEntry AuditEntry::FromJson(const nlohmann::json& j) {
  AuditEntry entry;
  entry.id = j.value("id", "");
  entry.event_type = StringToAuditEventType(j.value("event_type", ""));

  // 解析时间戳
  std::string timestamp_str = j.value("timestamp", "");
  if (!timestamp_str.empty()) {
    std::tm tm = {};
    std::istringstream ss(timestamp_str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (!ss.fail()) {
      // 使用 timegm 而不是 mktime，因为时间戳是 UTC
      #ifdef _WIN32
      entry.timestamp = std::chrono::system_clock::from_time_t(_mkgmtime(&tm));
      #else
      entry.timestamp = std::chrono::system_clock::from_time_t(timegm(&tm));
      #endif
    } else {
      entry.timestamp = std::chrono::system_clock::now();
    }
  } else {
    entry.timestamp = std::chrono::system_clock::now();
  }

  entry.user_id = j.value("user_id", "");
  entry.session_id = j.value("session_id", "");
  entry.source = j.value("source", "");
  entry.action = j.value("action", "");
  entry.details = j.value("details", nlohmann::json::object());
  entry.severity = j.value("severity", "info");

  return entry;
}

// SecurityAuditLogger 构造函数
SecurityAuditLogger::SecurityAuditLogger(
    std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
  // 默认日志目录
  const char* home = std::getenv("HOME");
  if (home) {
    log_directory_ = std::string(home) + "/.ravbot/logs/audit";
  } else {
    log_directory_ = "/tmp/ravbot/logs/audit";
  }

  // 确保目录存在
  std::filesystem::create_directories(log_directory_);
}

// 记录外部内容包装事件
// Requirements: 15.1
void SecurityAuditLogger::LogContentWrapping(
    const std::string& user_id,
    const std::string& session_id,
    const std::string& tool_name,
    const std::string& url,
    size_t content_size) {
  AuditEntry entry;
  entry.id = GenerateId();
  entry.event_type = AuditEventType::kContentWrapping;
  entry.timestamp = std::chrono::system_clock::now();
  entry.user_id = user_id;
  entry.session_id = session_id;
  entry.source = tool_name;
  entry.action = "wrap_external_content";
  entry.details = {
      {"tool_name", tool_name},
      {"url", url},
      {"content_size", content_size}
  };
  entry.severity = "info";

  LogEntry(entry);
}

// 记录边界标记清理事件
// Requirements: 15.2
void SecurityAuditLogger::LogMarkerSanitization(
    const std::string& user_id,
    const std::string& session_id,
    const std::string& source,
    int markers_removed) {
  AuditEntry entry;
  entry.id = GenerateId();
  entry.event_type = AuditEventType::kMarkerSanitization;
  entry.timestamp = std::chrono::system_clock::now();
  entry.user_id = user_id;
  entry.session_id = session_id;
  entry.source = source;
  entry.action = "sanitize_boundary_markers";
  entry.details = {
      {"markers_removed", markers_removed}
  };
  entry.severity = markers_removed > 0 ? "warning" : "info";

  LogEntry(entry);
}

// 记录危险工具调用事件
// Requirements: 15.3
void SecurityAuditLogger::LogDangerousToolCall(
    const std::string& user_id,
    const std::string& session_id,
    const std::string& tool_name,
    const nlohmann::json& parameters,
    bool approved) {
  AuditEntry entry;
  entry.id = GenerateId();
  entry.event_type = AuditEventType::kDangerousToolCall;
  entry.timestamp = std::chrono::system_clock::now();
  entry.user_id = user_id;
  entry.session_id = session_id;
  entry.source = "tool_execution";
  entry.action = "call_dangerous_tool";
  entry.details = {
      {"tool_name", tool_name},
      {"parameters", parameters},
      {"approved", approved}
  };
  entry.severity = approved ? "warning" : "error";

  LogEntry(entry);
}

// 记录认证失败事件
// Requirements: 15.4
void SecurityAuditLogger::LogAuthFailure(
    const std::string& user_id,
    const std::string& source,
    const std::string& reason) {
  AuditEntry entry;
  entry.id = GenerateId();
  entry.event_type = AuditEventType::kAuthFailure;
  entry.timestamp = std::chrono::system_clock::now();
  entry.user_id = user_id;
  entry.session_id = "";
  entry.source = source;
  entry.action = "authentication_failed";
  entry.details = {
      {"reason", reason}
  };
  entry.severity = "error";

  LogEntry(entry);
}

// 记录策略变更事件
// Requirements: 15.5
void SecurityAuditLogger::LogPolicyChange(
    const std::string& user_id,
    const std::string& policy_name,
    const nlohmann::json& old_value,
    const nlohmann::json& new_value) {
  AuditEntry entry;
  entry.id = GenerateId();
  entry.event_type = AuditEventType::kPolicyChange;
  entry.timestamp = std::chrono::system_clock::now();
  entry.user_id = user_id;
  entry.session_id = "";
  entry.source = "policy_manager";
  entry.action = "change_policy";
  entry.details = {
      {"policy_name", policy_name},
      {"old_value", old_value},
      {"new_value", new_value}
  };
  entry.severity = "warning";

  LogEntry(entry);
}

// 记录访问拒绝事件
void SecurityAuditLogger::LogAccessDenied(
    const std::string& user_id,
    const std::string& session_id,
    const std::string& resource,
    const std::string& reason) {
  AuditEntry entry;
  entry.id = GenerateId();
  entry.event_type = AuditEventType::kAccessDenied;
  entry.timestamp = std::chrono::system_clock::now();
  entry.user_id = user_id;
  entry.session_id = session_id;
  entry.source = "access_control";
  entry.action = "deny_access";
  entry.details = {
      {"resource", resource},
      {"reason", reason}
  };
  entry.severity = "warning";

  LogEntry(entry);
}

// 记录可疑活动事件
void SecurityAuditLogger::LogSuspiciousActivity(
    const std::string& user_id,
    const std::string& session_id,
    const std::string& description,
    const nlohmann::json& evidence) {
  AuditEntry entry;
  entry.id = GenerateId();
  entry.event_type = AuditEventType::kSuspiciousActivity;
  entry.timestamp = std::chrono::system_clock::now();
  entry.user_id = user_id;
  entry.session_id = session_id;
  entry.source = "security_monitor";
  entry.action = "detect_suspicious_activity";
  entry.details = {
      {"description", description},
      {"evidence", evidence}
  };
  entry.severity = "critical";

  LogEntry(entry);
}

// 导出审计日志
// Requirements: 15.6
std::vector<AuditEntry> SecurityAuditLogger::Export(
    const std::chrono::system_clock::time_point& start_time,
    const std::chrono::system_clock::time_point& end_time,
    const std::string& filter_user_id) const {
  std::vector<AuditEntry> results;

  // 遍历日志目录中的所有文件
  if (!std::filesystem::exists(log_directory_)) {
    return results;
  }

  for (const auto& entry : std::filesystem::directory_iterator(log_directory_)) {
    if (entry.path().extension() == ".jsonl") {
      auto entries = ReadLogFile(entry.path().string());
      for (const auto& audit_entry : entries) {
        // 过滤时间范围
        if (audit_entry.timestamp >= start_time &&
            audit_entry.timestamp <= end_time) {
          // 过滤用户 ID（如果指定）
          if (filter_user_id.empty() || audit_entry.user_id == filter_user_id) {
            results.push_back(audit_entry);
          }
        }
      }
    }
  }

  return results;
}

// 查询审计日志
// Requirements: 15.7
std::vector<AuditEntry> SecurityAuditLogger::Query(
    AuditEventType event_type,
    const std::chrono::system_clock::time_point& start_time,
    const std::chrono::system_clock::time_point& end_time) const {
  std::vector<AuditEntry> results;

  // 遍历日志目录中的所有文件
  if (!std::filesystem::exists(log_directory_)) {
    return results;
  }

  for (const auto& entry : std::filesystem::directory_iterator(log_directory_)) {
    if (entry.path().extension() == ".jsonl") {
      auto entries = ReadLogFile(entry.path().string());
      for (const auto& audit_entry : entries) {
        // 过滤事件类型和时间范围
        if (audit_entry.event_type == event_type &&
            audit_entry.timestamp >= start_time &&
            audit_entry.timestamp <= end_time) {
          results.push_back(audit_entry);
        }
      }
    }
  }

  return results;
}

// 设置日志目录
void SecurityAuditLogger::SetLogDirectory(const std::string& directory) {
  log_directory_ = directory;
  std::filesystem::create_directories(log_directory_);
}

// 获取当前日志文件路径
std::string SecurityAuditLogger::GetCurrentLogPath() const {
  return GetLogFilePath();
}

// 记录审计条目
void SecurityAuditLogger::LogEntry(const AuditEntry& entry) {
  // 写入 JSON Lines 格式文件
  std::string log_file = GetLogFilePath();
  std::ofstream file(log_file, std::ios::app);
  if (file.is_open()) {
    file << entry.ToJson().dump() << "\n";
  }

  // 同时记录到 spdlog
  if (logger_) {
    logger_->info("[AUDIT] {} - {} - {} - {}",
                  AuditEventTypeToString(entry.event_type),
                  entry.user_id,
                  entry.action,
                  entry.details.dump());
  }
}

// 生成唯一 ID
std::string SecurityAuditLogger::GenerateId() const {
  static std::random_device rd;
  static std::mt19937_64 gen(rd());
  static std::uniform_int_distribution<uint64_t> dis;

  std::ostringstream oss;
  oss << std::hex << std::setfill('0') << std::setw(16) << dis(gen);
  return oss.str();
}

// 获取当前日志文件路径（按日期）
std::string SecurityAuditLogger::GetLogFilePath() const {
  auto now = std::chrono::system_clock::now();
  auto time_t_val = std::chrono::system_clock::to_time_t(now);
  std::ostringstream oss;
  oss << log_directory_ << "/"
      << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%d")
      << ".jsonl";
  return oss.str();
}

// 读取日志文件
std::vector<AuditEntry> SecurityAuditLogger::ReadLogFile(
    const std::string& file_path) const {
  std::vector<AuditEntry> entries;

  std::ifstream file(file_path);
  if (!file.is_open()) {
    return entries;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) continue;

    try {
      nlohmann::json j = nlohmann::json::parse(line);
      entries.push_back(AuditEntry::FromJson(j));
    } catch (const std::exception& e) {
      // 忽略解析错误的行
      if (logger_) {
        logger_->warn("Failed to parse audit log line: {}", e.what());
      }
    }
  }

  return entries;
}

// 辅助函数：将 AuditEventType 转换为字符串
std::string AuditEventTypeToString(AuditEventType type) {
  switch (type) {
    case AuditEventType::kContentWrapping:
      return "content_wrapping";
    case AuditEventType::kMarkerSanitization:
      return "marker_sanitization";
    case AuditEventType::kDangerousToolCall:
      return "dangerous_tool_call";
    case AuditEventType::kAuthFailure:
      return "auth_failure";
    case AuditEventType::kPolicyChange:
      return "policy_change";
    case AuditEventType::kAccessDenied:
      return "access_denied";
    case AuditEventType::kSuspiciousActivity:
      return "suspicious_activity";
    default:
      return "unknown";
  }
}

// 辅助函数：将字符串转换为 AuditEventType
AuditEventType StringToAuditEventType(const std::string& str) {
  if (str == "content_wrapping") {
    return AuditEventType::kContentWrapping;
  } else if (str == "marker_sanitization") {
    return AuditEventType::kMarkerSanitization;
  } else if (str == "dangerous_tool_call") {
    return AuditEventType::kDangerousToolCall;
  } else if (str == "auth_failure") {
    return AuditEventType::kAuthFailure;
  } else if (str == "policy_change") {
    return AuditEventType::kPolicyChange;
  } else if (str == "access_denied") {
    return AuditEventType::kAccessDenied;
  } else if (str == "suspicious_activity") {
    return AuditEventType::kSuspiciousActivity;
  } else {
    return AuditEventType::kContentWrapping;  // 默认值
  }
}

}  // namespace ravbot
