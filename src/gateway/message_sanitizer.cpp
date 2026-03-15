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

// 清理助手输出消息
// Requirements: 7.1, 13.1
std::string MessageSanitizer::SanitizeOutput(const std::string& output) {
  // 1. 移除边界标记
  std::string sanitized = RemoveBoundaryMarkers(output);

  // 2. 移除系统内部标签
  sanitized = RemoveSystemTags(sanitized);

  return sanitized;
}

// 移除系统内部标签
// Requirements: 13.1
std::string MessageSanitizer::RemoveSystemTags(const std::string& text) {
  std::string result = text;

  // 定义需要移除的系统标签模式
  // 这些标签是系统内部使用的，不应该暴露给用户
  // 注意：使用 dotall 模式让 . 匹配换行符
  std::vector<std::regex> system_tag_patterns = {
      // <system.run>...</system.run>
      std::regex(R"(<system\.run>[\s\S]*?</system\.run>)", std::regex::icase),
      // <command>...</command>
      std::regex(R"(<command>[\s\S]*?</command>)", std::regex::icase),
      // <thinking>...</thinking>
      std::regex(R"(<thinking>[\s\S]*?</thinking>)", std::regex::icase),
      // <system>...</system>
      std::regex(R"(<system>[\s\S]*?</system>)", std::regex::icase),
      // <internal>...</internal>
      std::regex(R"(<internal>[\s\S]*?</internal>)", std::regex::icase),
      // <debug>...</debug>
      std::regex(R"(<debug>[\s\S]*?</debug>)", std::regex::icase),
      // <tool_use>...</tool_use>
      std::regex(R"(<tool_use>[\s\S]*?</tool_use>)", std::regex::icase),
      // <tool_result>...</tool_result>
      std::regex(R"(<tool_result>[\s\S]*?</tool_result>)", std::regex::icase),
      // <function_calls>...</function_calls>
      std::regex(R"(<function_calls>[\s\S]*?</function_calls>)", std::regex::icase),
      // <invoke>...</invoke>
      std::regex(R"(<invoke[^>]*>[\s\S]*?</invoke>)", std::regex::icase),
      // <parameter>...</parameter>
      std::regex(R"(<parameter[^>]*>[\s\S]*?</parameter>)", std::regex::icase),
      // <system-reminder>...</system-reminder>
      std::regex(R"(<system-reminder>[\s\S]*?</system-reminder>)", std::regex::icase),
      // Bash command outputs ($ command or # command)
      std::regex(R"(^\s*[$#]\s+.*$)", std::regex::multiline),
      // File paths with line numbers (file.cpp:123)
      std::regex(R"(\S+\.(cpp|hpp|h|c|py|js|ts):\d+)", std::regex::icase),
      // Tool execution markers
      std::regex(R"(Tool:\s*\w+)", std::regex::icase),
      std::regex(R"(Executing:\s*.*$)", std::regex::icase | std::regex::multiline),
  };

  // 应用所有过滤模式
  for (const auto& pattern : system_tag_patterns) {
    result = std::regex_replace(result, pattern, "");
  }

  return result;
}

// 清理外部内容元数据
// Requirements: 13.2
nlohmann::json MessageSanitizer::SanitizeMetadata(const nlohmann::json& metadata) {
  if (!metadata.is_object()) {
    return nlohmann::json::object();
  }

  nlohmann::json sanitized;

  // 允许的元数据字段白名单
  const std::vector<std::string> allowed_fields = {
      "source",      // 来源标识
      "timestamp",   // 时间戳
      "type",        // 内容类型
      "url",         // URL（如果是 web 内容）
      "title",       // 标题
      "author",      // 作者
      "language",    // 语言
  };

  // 只保留白名单中的字段
  for (const auto& field : allowed_fields) {
    if (metadata.contains(field)) {
      const auto& value = metadata[field];

      // 验证字段值类型和内容
      if (value.is_string()) {
        std::string str_value = value.get<std::string>();

        // 移除潜在的注入内容
        str_value = RemoveBoundaryMarkers(str_value);

        // 限制字符串长度
        if (str_value.size() > 1000) {
          str_value = str_value.substr(0, 1000);
        }

        sanitized[field] = str_value;
      } else if (value.is_number() || value.is_boolean()) {
        // 数字和布尔值直接保留
        sanitized[field] = value;
      }
      // 忽略其他类型（对象、数组等）
    }
  }

  return sanitized;
}

}  // namespace quantclaw
