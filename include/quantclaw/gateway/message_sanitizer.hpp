// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "quantclaw/gateway/protocol.hpp"

namespace quantclaw {

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
  bool NormalizeAttachment(Attachment& attachment);

  // 验证消息结构完整性
  // Requirements: 7.1, 7.2
  bool ValidateMessage(const Message& message);

 private:
  // 移除所有边界标记
  // Requirements: 13.1, 13.2
  std::string RemoveBoundaryMarkers(const std::string& text);

  // 已知的边界标记模式
  std::vector<std::string> boundary_markers_;

  // 最大消息大小（字节）
  static constexpr size_t kMaxMessageSize = 1024 * 1024;  // 1MB

  // 最大附件大小（字节）
  static constexpr size_t kMaxAttachmentSize = 10 * 1024 * 1024;  // 10MB
};

}  // namespace quantclaw
