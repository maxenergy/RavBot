// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <nlohmann/json.hpp>

namespace ravbot {

// 信任级别枚举
// Requirements: 14.1, 14.2, 14.3, 14.4, 14.5
enum class TrustLevel {
  kTrusted,       // 完全可信（本地文件、系统消息）
  kSemiTrusted,   // 半可信（用户输入）
  kUntrusted      // 不可信（外部内容、Web 搜索）
};

// 安全策略
// Requirements: 14.6
struct SecurityPolicy {
  bool allow_dangerous_tools = false;    // 是否允许危险工具调用
  bool require_approval = false;         // 是否需要审批
  bool enable_sandboxing = false;        // 是否启用沙箱
  size_t max_content_size = 0;           // 最大内容大小（0 表示无限制）
  bool log_all_actions = false;          // 是否记录所有操作

  nlohmann::json ToJson() const;
  static SecurityPolicy FromJson(const nlohmann::json& j);
};

// 信任模型管理器
// Requirements: 14.1-14.8
class TrustModelManager {
 public:
  TrustModelManager();

  // 获取内容来源的信任级别
  // Requirements: 14.2, 14.3, 14.4, 14.5
  TrustLevel GetTrustLevel(const std::string& source_type) const;

  // 设置工具的信任级别
  // Requirements: 14.6
  void SetToolTrustLevel(const std::string& tool_name, TrustLevel level);

  // 获取工具的信任级别
  TrustLevel GetToolTrustLevel(const std::string& tool_name) const;

  // 设置发送者的信任级别
  // Requirements: 14.7
  void SetSenderTrustLevel(const std::string& sender_id, TrustLevel level);

  // 获取发送者的信任级别
  TrustLevel GetSenderTrustLevel(const std::string& sender_id) const;

  // 获取信任级别对应的安全策略
  // Requirements: 14.6
  SecurityPolicy GetPolicy(TrustLevel level) const;

  // 设置信任级别对应的安全策略
  void SetPolicy(TrustLevel level, const SecurityPolicy& policy);

  // 持久化配置
  // Requirements: 14.8
  void Save(const std::string& path) const;
  void Load(const std::string& path);

  // 重置为默认配置
  void Reset();

 private:
  // 初始化默认策略
  void InitializeDefaultPolicies();

  // 默认来源类型到信任级别的映射
  std::unordered_map<std::string, TrustLevel> default_source_trust_;

  // 工具名称到信任级别的映射
  std::unordered_map<std::string, TrustLevel> tool_trust_;

  // 发送者 ID 到信任级别的映射
  std::unordered_map<std::string, TrustLevel> sender_trust_;

  // 信任级别到安全策略的映射
  std::unordered_map<TrustLevel, SecurityPolicy> policies_;

  // 线程安全
  mutable std::shared_mutex mutex_;
};

// 辅助函数：将 TrustLevel 转换为字符串
std::string TrustLevelToString(TrustLevel level);

// 辅助函数：将字符串转换为 TrustLevel
TrustLevel StringToTrustLevel(const std::string& str);

}  // namespace ravbot
