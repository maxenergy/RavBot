// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace ravbot {

// 内容来源类型枚举，与 OpenClaw ExternalContentSource 对齐
// Requirements: 12.1
enum class ExternalContentSource {
  kEmail,
  kWebhook,
  kApi,
  kBrowser,
  kChannelMetadata,
  kWebSearch,
  kWebFetch,
  kUnknown,
};

// 内容来源信息（保留向后兼容的 ContentSource，同时新增 ExternalContentSource）
// Requirements: 12.1, 12.2
struct ContentSource {
  std::string tool_name;    // 工具名称（如 "web_search", "web_fetch"）
  std::string url;          // 来源 URL（如果适用）
  std::string timestamp;    // 获取时间戳
  std::string content_type; // 内容类型（如 "html", "json", "text"）

  nlohmann::json ToJson() const;
  static ContentSource FromJson(const nlohmann::json& j);
};

// 外部内容包装器：用随机ID边界标记包装不可信的外部内容
// 每次调用 Wrap() 生成唯一随机 ID，防止攻击者伪造边界标记
// Requirements: 12.1, 12.2, 12.3, 12.4, 12.5, 12.6, 12.7, 12.8
class ExternalContentWrapper {
 public:
  ExternalContentWrapper() = default;

  // 检测内容中可能的 Prompt 注入模式（返回匹配到的模式源字符串列表）
  // Requirements: 12.4, 12.8
  static std::vector<std::string> DetectSuspiciousPatterns(
      const std::string& content);

  // Unicode 同形字规范化：将全角字母/CJK角括号等映射为 ASCII 等价字符
  // 用于防御通过全角字符绕过标记检测的攻击
  // Requirements: 12.4
  static std::string FoldMarkerText(const std::string& input);

  // 用随机 ID 边界标记包装外部内容（每次调用生成新唯一 ID）
  // Requirements: 12.2, 12.3
  std::string Wrap(const std::string& content,
                   const ContentSource& source) const;

  // 解包内容（用于测试/调试），使用正则匹配随机 ID 格式
  // Requirements: 12.2
  std::optional<std::string> Unwrap(const std::string& wrapped) const;

  // 验证边界标记的完整性（含 Unicode 同形字折叠检测）
  // Requirements: 12.5
  bool ValidateMarkers(const std::string& wrapped) const;

  // 获取边界标记说明（用于系统提示词）
  // Requirements: 12.6, 12.7
  std::string GetMarkerExplanation() const;

  // 随机 ID 边界标记的标签名（供外部使用）
  static constexpr const char* kStartName =
      "EXTERNAL_UNTRUSTED_CONTENT";
  static constexpr const char* kEndName =
      "END_EXTERNAL_UNTRUSTED_CONTENT";

  // 内联安全警告文本（随内容一起传递）
  // Requirements: 12.7
  static const char* kExternalContentWarning;

 private:
  // 净化内容中可能伪造的边界标记（先折叠同形字再替换）
  static std::string replace_markers(const std::string& content);

  // 生成随机 16 位十六进制 ID（使用 OpenSSL RAND_bytes）
  static std::string generate_marker_id();

  // 构造开始/结束标记字符串
  static std::string make_start_marker(const std::string& id);
  static std::string make_end_marker(const std::string& id);
};

}  // namespace ravbot
