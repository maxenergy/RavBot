// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/gateway/message_sanitizer.hpp"

#include <algorithm>
#include <regex>

namespace quantclaw {

MessageSanitizer::MessageSanitizer() {
  // 定义已知的边界标记模式
  // 使用特殊 Unicode 字符组合，难以伪造
  boundary_markers_ = {
      "⟨⟨EXTERNAL_CONTENT⟩⟩",
      "⟨⟨/EXTERNAL_CONTENT⟩⟩",
      "⟨⟨UNTRUSTED⟩⟩",
      "⟨⟨/UNTRUSTED⟩⟩",
      "⟨⟨WEB_SEARCH⟩⟩",
      "⟨⟨/WEB_SEARCH⟩⟩",
      "⟨⟨WEB_FETCH⟩⟩",
      "⟨⟨/WEB_FETCH⟩⟩",
  };
}

// 清理用户输入消息
// Requirements: 7.1, 13.1
std::string MessageSanitizer::SanitizeInput(const std::string& input) {
  // 1. 移除边界标记
  std::string sanitized = RemoveBoundaryMarkers(input);

  // 2. 验证大小限制
  if (sanitized.size() > kMaxMessageSize) {
    sanitized = sanitized.substr(0, kMaxMessageSize);
  }

  // 3. 移除控制字符（保留换行和制表符）
  sanitized.erase(
      std::remove_if(sanitized.begin(), sanitized.end(),
                     [](unsigned char c) {
                       return (c < 32 && c != '\n' && c != '\t' && c != '\r');
                     }),
      sanitized.end());

  return sanitized;
}

// 规范化附件格式
// Requirements: 7.2
nlohmann::json MessageSanitizer::NormalizeAttachment(const nlohmann::json& attachment) {
  nlohmann::json normalized = attachment;

  // 验证附件必需字段
  if (!attachment.contains("data") || !attachment.contains("mime_type")) {
    return nlohmann::json::object();  // 返回空对象表示无效
  }

  // 验证附件大小
  std::string data = attachment["data"].get<std::string>();
  if (data.size() > kMaxAttachmentSize) {
    return nlohmann::json::object();  // 超过大小限制
  }

  // 规范化 MIME 类型为小写
  std::string mime_type = attachment["mime_type"].get<std::string>();
  std::transform(mime_type.begin(), mime_type.end(), mime_type.begin(), ::tolower);
  normalized["mime_type"] = mime_type;

  return normalized;
}

// 验证消息结构完整性
// Requirements: 7.1, 7.2
bool MessageSanitizer::ValidateMessage(const nlohmann::json& message) {
  // 验证消息必需字段
  if (!message.contains("content") && !message.contains("attachments")) {
    return false;
  }

  // 验证角色有效
  if (message.contains("role")) {
    std::string role = message["role"].get<std::string>();
    if (role != "user" && role != "assistant" && role != "system") {
      return false;
    }
  }

  // 验证所有附件
  if (message.contains("attachments") && message["attachments"].is_array()) {
    for (const auto& attachment : message["attachments"]) {
      if (!attachment.contains("data")) {
        return false;
      }
      std::string data = attachment["data"].get<std::string>();
      if (data.size() > kMaxAttachmentSize) {
        return false;
      }
    }
  }

  return true;
}

// 移除所有边界标记
// Requirements: 13.1, 13.2
std::string MessageSanitizer::RemoveBoundaryMarkers(const std::string& text) {
  std::string result = text;

  // 移除所有已知的边界标记
  for (const auto& marker : boundary_markers_) {
    size_t pos = 0;
    while ((pos = result.find(marker, pos)) != std::string::npos) {
      result.erase(pos, marker.length());
    }
  }

  // 移除边界标记的变体（大小写、空格等）
  // 使用正则表达式匹配常见变体
  std::regex marker_pattern(
      R"(⟨⟨\s*[/]?\s*(EXTERNAL_CONTENT|UNTRUSTED|WEB_SEARCH|WEB_FETCH)\s*⟩⟩)",
      std::regex::icase);
  result = std::regex_replace(result, marker_pattern, "");

  return result;
}

}  // namespace quantclaw
