// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace quantclaw {

// 内容来源信息
// Requirements: 12.1, 12.2
struct ContentSource {
  std::string tool_name;      // 工具名称（如 "web_search", "web_fetch"）
  std::string url;             // 来源 URL（如果适用）
  std::string timestamp;       // 获取时间戳
  std::string content_type;    // 内容类型（如 "html", "json", "text"）

  nlohmann::json ToJson() const;
  static ContentSource FromJson(const nlohmann::json& j);
};

// 外部内容包装器：用边界标记包装不可信的外部内容
// Requirements: 12.1, 12.2, 12.3, 12.4, 12.5, 12.6, 12.7, 12.8
class ExternalContentWrapper {
 public:
  ExternalContentWrapper() = default;

  // 用边界标记包装外部内容
  // Requirements: 12.2, 12.3
  std::string Wrap(const std::string& content,
                   const ContentSource& source) const;

  // 解包内容（用于测试/调试）
  // Requirements: 12.2
  std::optional<std::string> Unwrap(const std::string& wrapped) const;

  // 验证边界标记的完整性
  // Requirements: 12.5
  bool ValidateMarkers(const std::string& wrapped) const;

  // 获取边界标记说明（用于系统提示词）
  // Requirements: 12.6, 12.7
  std::string GetMarkerExplanation() const;

 private:
  // 生成开始标记
  std::string start_marker() const;

  // 生成结束标记
  std::string end_marker() const;

  // 边界标记使用特殊 Unicode 字符，难以伪造
  // Requirements: 12.3, 12.4
  static constexpr const char* kStartMarker = "⟦EXTERNAL_CONTENT_START⟧";
  static constexpr const char* kEndMarker = "⟦EXTERNAL_CONTENT_END⟧";
};

}  // namespace quantclaw
