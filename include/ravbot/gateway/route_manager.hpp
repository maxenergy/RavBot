// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <spdlog/spdlog.h>

namespace ravbot {

// 消息优先级
enum class MessagePriority {
  kLow = 0,
  kNormal = 1,
  kHigh = 2,
  kUrgent = 3
};

// 交付状态
enum class DeliveryStatus {
  kPending,    // 等待交付
  kDelivered,  // 已交付
  kFailed      // 交付失败
};

// 路由元数据
// Requirements: 6.1, 6.2, 6.3, 6.4
struct RouteMetadata {
  std::string message_id;           // 唯一消息 ID
  std::string source_channel;       // 来源渠道
  std::string target_session;       // 目标会话
  std::string parent_message_id;    // 父消息 ID（用于继承）
  std::chrono::system_clock::time_point timestamp;  // 时间戳
  MessagePriority priority = MessagePriority::kNormal;  // 优先级
  DeliveryStatus status = DeliveryStatus::kPending;     // 交付状态
  bool is_external = false;  // 是否来自外部渠道
};

// 路由管理器：管理消息路由和交付状态
// Requirements: 6.1-6.8
class RouteManager {
 public:
  explicit RouteManager(std::shared_ptr<spdlog::logger> logger);

  // 为消息附加路由元数据
  // Requirements: 6.1
  RouteMetadata AttachMetadata(const std::string& source_channel,
                                const std::string& target_session,
                                bool is_external = false);

  // 从父消息继承路由信息
  // Requirements: 6.2
  RouteMetadata InheritRoute(const std::string& parent_message_id,
                              const std::string& target_session);

  // 记录消息交付状态
  // Requirements: 6.5
  void RecordDelivery(const std::string& message_id, DeliveryStatus status);

  // 获取消息的交付状态
  // Requirements: 6.6
  DeliveryStatus GetDeliveryStatus(const std::string& message_id) const;

  // 查询特定会话的所有消息
  // Requirements: 6.6
  std::vector<RouteMetadata> GetSessionMessages(
      const std::string& session_id) const;

  // 设置消息优先级
  // Requirements: 6.8
  void SetPriority(const std::string& message_id, MessagePriority priority);

 private:
  // 生成唯一的消息 ID
  std::string generate_message_id();

  std::shared_ptr<spdlog::logger> logger_;
  mutable std::mutex mu_;

  // message_id -> RouteMetadata
  std::unordered_map<std::string, RouteMetadata> routes_;

  // session_id -> [message_ids]
  std::unordered_map<std::string, std::vector<std::string>> session_messages_;

  // 消息 ID 计数器
  uint64_t message_counter_ = 0;
};

}  // namespace ravbot
