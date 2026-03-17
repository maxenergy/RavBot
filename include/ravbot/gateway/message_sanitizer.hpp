// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ravbot {

// 消息清理器：移除潜在的恶意内容和边界标记
// Requirements: 7.1, 7.2, 13.1, 13.2
class MessageSanitizer {
 public:
  MessageSanitizer();

  // 清理用户输入消息
  // 移除边界标记、潜在的恶意内容
  // Requirements: 7.1, 13.1
  std::string SanitizeInput(const std::string& input);

  // 规范化附件格式
  // 验证附件类型和大小
  // Requirements: 7.2
  nlohmann::json NormalizeAttachment(const nlohmann::json& attachment);

  // 验证消息结构完整性
  // Requirements: 7.1, 7.2
  bool ValidateMessage(const nlohmann::json& message);

  // 清理助手输出消息
  // 移除系统内部标签，防止泄露给用户
  // Requirements: 7.1, 13.1
  std::string SanitizeOutput(const std::string& output);

  // 清理外部内容元数据
  // 移除可能包含注入攻击的元数据字段
  // Requirements: 13.2
  nlohmann::json SanitizeMetadata(const nlohmann::json& metadata);

 private:
  // 移除所有边界标记
  // Requirements: 13.1, 13.2
  std::string RemoveBoundaryMarkers(const std::string& text);

  // 移除系统内部标签
  // Requirements: 13.1
  std::string RemoveSystemTags(const std::string& text);

  // 已知的边界标记模式
  std::vector<std::string> boundary_markers_;

  // 最大消息大小（字节）
  static constexpr size_t kMaxMessageSize = 1024 * 1024;  // 1MB

  // 最大附件大小（字节）
  static constexpr size_t kMaxAttachmentSize = 10 * 1024 * 1024;  // 10MB
};

}  // namespace ravbot
