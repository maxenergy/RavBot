// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/security/trust_model.hpp"
#include <fstream>
#include <stdexcept>

namespace quantclaw {

// SecurityPolicy 序列化
nlohmann::json SecurityPolicy::ToJson() const {
  return {
      {"allow_dangerous_tools", allow_dangerous_tools},
      {"require_approval", require_approval},
      {"enable_sandboxing", enable_sandboxing},
      {"max_content_size", max_content_size},
      {"log_all_actions", log_all_actions}
  };
}

// SecurityPolicy 反序列化
SecurityPolicy SecurityPolicy::FromJson(const nlohmann::json& j) {
  SecurityPolicy policy;
  policy.allow_dangerous_tools = j.value("allow_dangerous_tools", false);
  policy.require_approval = j.value("require_approval", false);
  policy.enable_sandboxing = j.value("enable_sandboxing", false);
  policy.max_content_size = j.value("max_content_size", 0);
  policy.log_all_actions = j.value("log_all_actions", false);
  return policy;
}

// TrustModelManager 构造函数
TrustModelManager::TrustModelManager() {
  // 初始化默认来源类型映射
  // Requirements: 14.2, 14.3, 14.4, 14.5
  default_source_trust_["user_input"] = TrustLevel::kSemiTrusted;
  default_source_trust_["web_search"] = TrustLevel::kUntrusted;
  default_source_trust_["web_fetch"] = TrustLevel::kUntrusted;
  default_source_trust_["local_file"] = TrustLevel::kTrusted;
  default_source_trust_["system"] = TrustLevel::kTrusted;
  default_source_trust_["channel_message"] = TrustLevel::kSemiTrusted;

  // 初始化默认策略
  InitializeDefaultPolicies();
}

// 初始化默认策略
// Requirements: 14.6
void TrustModelManager::InitializeDefaultPolicies() {
  // Trusted: 最宽松的策略
  SecurityPolicy trusted_policy;
  trusted_policy.allow_dangerous_tools = true;
  trusted_policy.require_approval = false;
  trusted_policy.enable_sandboxing = false;
  trusted_policy.max_content_size = 0;  // 无限制
  trusted_policy.log_all_actions = false;
  policies_[TrustLevel::kTrusted] = trusted_policy;

  // SemiTrusted: 中等策略
  SecurityPolicy semi_trusted_policy;
  semi_trusted_policy.allow_dangerous_tools = true;
  semi_trusted_policy.require_approval = true;  // 需要审批危险操作
  semi_trusted_policy.enable_sandboxing = false;
  semi_trusted_policy.max_content_size = 10 * 1024 * 1024;  // 10MB
  semi_trusted_policy.log_all_actions = true;
  policies_[TrustLevel::kSemiTrusted] = semi_trusted_policy;

  // Untrusted: 最严格的策略
  SecurityPolicy untrusted_policy;
  untrusted_policy.allow_dangerous_tools = false;
  untrusted_policy.require_approval = true;
  untrusted_policy.enable_sandboxing = true;
  untrusted_policy.max_content_size = 1 * 1024 * 1024;  // 1MB
  untrusted_policy.log_all_actions = true;
  policies_[TrustLevel::kUntrusted] = untrusted_policy;
}

// 获取内容来源的信任级别
// Requirements: 14.2, 14.3, 14.4, 14.5
TrustLevel TrustModelManager::GetTrustLevel(
    const std::string& source_type) const {
  std::shared_lock lock(mutex_);

  auto it = default_source_trust_.find(source_type);
  if (it != default_source_trust_.end()) {
    return it->second;
  }

  // 默认为不可信
  return TrustLevel::kUntrusted;
}

// 设置工具的信任级别
// Requirements: 14.6
void TrustModelManager::SetToolTrustLevel(const std::string& tool_name,
                                           TrustLevel level) {
  std::unique_lock lock(mutex_);
  tool_trust_[tool_name] = level;
}

// 获取工具的信任级别
TrustLevel TrustModelManager::GetToolTrustLevel(
    const std::string& tool_name) const {
  std::shared_lock lock(mutex_);

  auto it = tool_trust_.find(tool_name);
  if (it != tool_trust_.end()) {
    return it->second;
  }

  // 默认为半可信
  return TrustLevel::kSemiTrusted;
}

// 设置发送者的信任级别
// Requirements: 14.7
void TrustModelManager::SetSenderTrustLevel(const std::string& sender_id,
                                             TrustLevel level) {
  std::unique_lock lock(mutex_);
  sender_trust_[sender_id] = level;
}

// 获取发送者的信任级别
TrustLevel TrustModelManager::GetSenderTrustLevel(
    const std::string& sender_id) const {
  std::shared_lock lock(mutex_);

  auto it = sender_trust_.find(sender_id);
  if (it != sender_trust_.end()) {
    return it->second;
  }

  // 默认为半可信
  return TrustLevel::kSemiTrusted;
}

// 获取信任级别对应的安全策略
// Requirements: 14.6
SecurityPolicy TrustModelManager::GetPolicy(TrustLevel level) const {
  std::shared_lock lock(mutex_);

  auto it = policies_.find(level);
  if (it != policies_.end()) {
    return it->second;
  }

  // 默认返回最严格的策略
  return policies_.at(TrustLevel::kUntrusted);
}

// 设置信任级别对应的安全策略
void TrustModelManager::SetPolicy(TrustLevel level,
                                   const SecurityPolicy& policy) {
  std::unique_lock lock(mutex_);
  policies_[level] = policy;
}

// 持久化配置
// Requirements: 14.8
void TrustModelManager::Save(const std::string& path) const {
  std::shared_lock lock(mutex_);

  nlohmann::json config;

  // 保存工具信任级别
  nlohmann::json tool_trust_json = nlohmann::json::object();
  for (const auto& [tool, level] : tool_trust_) {
    tool_trust_json[tool] = TrustLevelToString(level);
  }
  config["tool_trust"] = tool_trust_json;

  // 保存发送者信任级别
  nlohmann::json sender_trust_json = nlohmann::json::object();
  for (const auto& [sender, level] : sender_trust_) {
    sender_trust_json[sender] = TrustLevelToString(level);
  }
  config["sender_trust"] = sender_trust_json;

  // 保存策略
  nlohmann::json policies_json = nlohmann::json::object();
  for (const auto& [level, policy] : policies_) {
    policies_json[TrustLevelToString(level)] = policy.ToJson();
  }
  config["policies"] = policies_json;

  // 写入文件
  std::ofstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file for writing: " + path);
  }
  file << config.dump(2);
}

// 加载配置
void TrustModelManager::Load(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file for reading: " + path);
  }

  nlohmann::json config;
  file >> config;

  std::unique_lock lock(mutex_);

  // 加载工具信任级别
  if (config.contains("tool_trust")) {
    tool_trust_.clear();
    for (auto& [tool, level_str] : config["tool_trust"].items()) {
      tool_trust_[tool] = StringToTrustLevel(level_str.get<std::string>());
    }
  }

  // 加载发送者信任级别
  if (config.contains("sender_trust")) {
    sender_trust_.clear();
    for (auto& [sender, level_str] : config["sender_trust"].items()) {
      sender_trust_[sender] = StringToTrustLevel(level_str.get<std::string>());
    }
  }

  // 加载策略
  if (config.contains("policies")) {
    for (auto& [level_str, policy_json] : config["policies"].items()) {
      TrustLevel level = StringToTrustLevel(level_str);
      policies_[level] = SecurityPolicy::FromJson(policy_json);
    }
  }
}

// 重置为默认配置
void TrustModelManager::Reset() {
  std::unique_lock lock(mutex_);
  tool_trust_.clear();
  sender_trust_.clear();
  policies_.clear();
  InitializeDefaultPolicies();
}

// 辅助函数：将 TrustLevel 转换为字符串
std::string TrustLevelToString(TrustLevel level) {
  switch (level) {
    case TrustLevel::kTrusted:
      return "trusted";
    case TrustLevel::kSemiTrusted:
      return "semi_trusted";
    case TrustLevel::kUntrusted:
      return "untrusted";
    default:
      return "unknown";
  }
}

// 辅助函数：将字符串转换为 TrustLevel
TrustLevel StringToTrustLevel(const std::string& str) {
  if (str == "trusted") {
    return TrustLevel::kTrusted;
  } else if (str == "semi_trusted") {
    return TrustLevel::kSemiTrusted;
  } else if (str == "untrusted") {
    return TrustLevel::kUntrusted;
  } else {
    throw std::invalid_argument("Invalid trust level string: " + str);
  }
}

}  // namespace quantclaw
