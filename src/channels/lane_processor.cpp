// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/channels/lane_processor.hpp"

namespace ravbot {

LaneProcessor::LaneProcessor(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

LaneProcessor::~LaneProcessor() {
  Shutdown();
}

// 提交消息到 lane 进行顺序处理
// Requirements: 9.4, 9.5
void LaneProcessor::Submit(const std::string& lane_id,
                             std::function<void()> handler) {
  if (shutdown_.load()) {
    logger_->warn("LaneProcessor is shutting down, rejecting submission");
    return;
  }

  Lane* lane = nullptr;

  // 获取或创建 lane
  {
    std::unique_lock<std::shared_mutex> lock(lanes_mutex_);
    auto it = lanes_.find(lane_id);
    if (it == lanes_.end()) {
      // 创建新 lane
      auto new_lane = std::make_unique<Lane>();
      lane = new_lane.get();
      lanes_[lane_id] = std::move(new_lane);

      // 启动工作线程
      lane->worker = std::make_unique<std::thread>(
          &LaneProcessor::worker_thread, this, lane_id, lane);

      logger_->info("Created new lane: {}", lane_id);
    } else {
      lane = it->second.get();
    }
  }

  // 提交任务到队列
  {
    std::lock_guard<std::mutex> lock(lane->mutex);
    lane->queue.push(std::move(handler));
  }
  lane->cv.notify_one();

  logger_->debug("Submitted task to lane: {}, queue_size={}", lane_id,
                 GetQueueSize(lane_id));
}

// 获取 lane 队列大小
// Requirements: 9.6
size_t LaneProcessor::GetQueueSize(const std::string& lane_id) const {
  std::shared_lock<std::shared_mutex> lock(lanes_mutex_);
  auto it = lanes_.find(lane_id);
  if (it == lanes_.end()) {
    return 0;
  }

  std::lock_guard<std::mutex> lane_lock(it->second->mutex);
  return it->second->queue.size();
}

// 关闭所有 lane
// Requirements: 9.6
void LaneProcessor::Shutdown() {
  if (shutdown_.exchange(true)) {
    return;  // 已经关闭
  }

  logger_->info("Shutting down LaneProcessor");

  std::unique_lock<std::shared_mutex> lock(lanes_mutex_);

  // 停止所有 lane
  for (auto& [lane_id, lane] : lanes_) {
    lane->running.store(false);
    lane->cv.notify_one();

    if (lane->worker && lane->worker->joinable()) {
      lane->worker->join();
    }

    logger_->debug("Stopped lane: {}", lane_id);
  }

  lanes_.clear();
  logger_->info("LaneProcessor shutdown complete");
}

// 工作线程
void LaneProcessor::worker_thread(const std::string& lane_id, Lane* lane) {
  logger_->debug("Lane worker started: {}", lane_id);

  while (lane->running.load()) {
    std::function<void()> handler;

    // 等待任务
    {
      std::unique_lock<std::mutex> lock(lane->mutex);
      lane->cv.wait(lock, [lane] {
        return !lane->queue.empty() || !lane->running.load();
      });

      if (!lane->running.load() && lane->queue.empty()) {
        break;
      }

      if (!lane->queue.empty()) {
        handler = std::move(lane->queue.front());
        lane->queue.pop();
      }
    }

    // 执行任务
    if (handler) {
      try {
        handler();
      } catch (const std::exception& e) {
        logger_->error("Lane handler error: lane={}, error={}", lane_id,
                       e.what());
      }
    }
  }

  logger_->debug("Lane worker stopped: {}", lane_id);
}

}  // namespace ravbot
