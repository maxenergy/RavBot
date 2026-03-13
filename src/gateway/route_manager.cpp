// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/gateway/route_manager.hpp"

#include <iomanip>
#include <sstream>

namespace quantclaw {

RouteManager::RouteManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

// 为消息附加路由元数据
// Requirements: 6.1
RouteMetadata RouteManager::AttachMetadata(const std::string& source_channel,
                                            const std::string& target_session,
                                            bool is_external) {
  std::lock_guard<std::mutex> lock(mu_);

  RouteMetadata metadata;
  metadata.message_id = generate_message_id();
  metadata.source_channel = source_channel;
  metadata.target_session = target_session;
  metadata.timestamp = std::chrono::system_clock::now();
  metadata.is_external = is_external;
  metadata.priority = MessagePriority::kNormal;
  metadata.status = DeliveryStatus::kPending;

  // 保存路由信息
  routes_[metadata.message_id] = metadata;
  session_messages_[target_session].push_back(metadata.message_id);

  logger_->debug("Attached route metadata: msg_id={}, source={}, target={}, "
                 "external={}",
                 metadata.message_id, source_channel, target_session,
                 is_external);

  return metadata;
}

// 从父消息继承路由信息
// Requirements: 6.2
RouteMetadata RouteManager::InheritRoute(const std::string& parent_message_id,
                                          const std::string& target_session) {
  std::lock_guard<std::mutex> lock(mu_);

  RouteMetadata metadata;
  metadata.message_id = generate_message_id();
  metadata.parent_message_id = parent_message_id;
  metadata.target_session = target_session;
  metadata.timestamp = std::chrono::system_clock::now();
  metadata.priority = MessagePriority::kNormal;
  metadata.status = DeliveryStatus::kPending;

  // 从父消息继承属性
  auto parent_it = routes_.find(parent_message_id);
  if (parent_it != routes_.end()) {
    metadata.source_channel = parent_it->second.source_channel;
    metadata.is_external = parent_it->second.is_external;
    metadata.priority = parent_it->second.priority;

    logger_->debug("Inherited route from parent: msg_id={}, parent={}, "
                   "source={}, external={}",
                   metadata.message_id, parent_message_id,
                   metadata.source_channel, metadata.is_external);
  } else {
    logger_->warn("Parent message not found for inheritance: parent={}",
                  parent_message_id);
  }

  // 保存路由信息
  routes_[metadata.message_id] = metadata;
  session_messages_[target_session].push_back(metadata.message_id);

  return metadata;
}

// 记录消息交付状态
// Requirements: 6.5
void RouteManager::RecordDelivery(const std::string& message_id,
                                   DeliveryStatus status) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = routes_.find(message_id);
  if (it != routes_.end()) {
    it->second.status = status;

    const char* status_str = "unknown";
    switch (status) {
      case DeliveryStatus::kPending:
        status_str = "pending";
        break;
      case DeliveryStatus::kDelivered:
        status_str = "delivered";
        break;
      case DeliveryStatus::kFailed:
        status_str = "failed";
        break;
    }

    logger_->debug("Recorded delivery status: msg_id={}, status={}",
                   message_id, status_str);
  } else {
    logger_->warn("Message not found for delivery recording: msg_id={}",
                  message_id);
  }
}

// 获取消息的交付状态
// Requirements: 6.6
DeliveryStatus RouteManager::GetDeliveryStatus(
    const std::string& message_id) const {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = routes_.find(message_id);
  if (it != routes_.end()) {
    return it->second.status;
  }

  return DeliveryStatus::kPending;
}

// 查询特定会话的所有消息
// Requirements: 6.6
std::vector<RouteMetadata> RouteManager::GetSessionMessages(
    const std::string& session_id) const {
  std::lock_guard<std::mutex> lock(mu_);

  std::vector<RouteMetadata> messages;

  auto it = session_messages_.find(session_id);
  if (it != session_messages_.end()) {
    for (const auto& msg_id : it->second) {
      auto route_it = routes_.find(msg_id);
      if (route_it != routes_.end()) {
        messages.push_back(route_it->second);
      }
    }
  }

  return messages;
}

// 设置消息优先级
// Requirements: 6.8
void RouteManager::SetPriority(const std::string& message_id,
                                MessagePriority priority) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = routes_.find(message_id);
  if (it != routes_.end()) {
    it->second.priority = priority;

    const char* priority_str = "normal";
    switch (priority) {
      case MessagePriority::kLow:
        priority_str = "low";
        break;
      case MessagePriority::kNormal:
        priority_str = "normal";
        break;
      case MessagePriority::kHigh:
        priority_str = "high";
        break;
      case MessagePriority::kUrgent:
        priority_str = "urgent";
        break;
    }

    logger_->debug("Set message priority: msg_id={}, priority={}", message_id,
                   priority_str);
  }
}

// 生成唯一的消息 ID
std::string RouteManager::generate_message_id() {
  // 格式: msg_<timestamp>_<counter>
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();

  std::ostringstream oss;
  oss << "msg_" << timestamp << "_" << message_counter_++;
  return oss.str();
}

}  // namespace quantclaw
