// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <spdlog/spdlog.h>

namespace ravbot {

// Lane 处理器：为每个 lane（如 chat_id）提供顺序处理保证
// Requirements: 9.4, 9.5, 9.6
class LaneProcessor {
 public:
  explicit LaneProcessor(std::shared_ptr<spdlog::logger> logger);
  ~LaneProcessor();

  // 提交消息到 lane 进行顺序处理
  // Requirements: 9.4, 9.5
  void Submit(const std::string& lane_id, std::function<void()> handler);

  // 获取 lane 队列大小
  // Requirements: 9.6
  size_t GetQueueSize(const std::string& lane_id) const;

  // 关闭所有 lane
  // Requirements: 9.6
  void Shutdown();

 private:
  struct Lane {
    std::queue<std::function<void()>> queue;
    std::unique_ptr<std::thread> worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> running{true};
  };

  void worker_thread(const std::string& lane_id, Lane* lane);

  std::shared_ptr<spdlog::logger> logger_;
  std::map<std::string, std::unique_ptr<Lane>> lanes_;
  mutable std::shared_mutex lanes_mutex_;
  std::atomic<bool> shutdown_{false};
};

}  // namespace ravbot
