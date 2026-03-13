// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/channels/deduplication_store.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

namespace quantclaw {

DeduplicationStore::DeduplicationStore(const std::filesystem::path& store_path,
                                         std::shared_ptr<spdlog::logger> logger)
    : store_path_(store_path), logger_(std::move(logger)) {
  // 确保目录存在
  if (!store_path_.parent_path().empty()) {
    std::filesystem::create_directories(store_path_.parent_path());
  }
}

// 检查 update_id 是否已处理
// Requirements: 9.1, 9.3
bool DeduplicationStore::IsProcessed(int64_t update_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  // 如果 update_id 小于等于水位线，认为已处理
  if (update_id <= watermark_) {
    return true;
  }

  // 检查是否在已处理集合中
  return processed_ids_.find(update_id) != processed_ids_.end();
}

// 标记 update_id 为已处理
// Requirements: 9.1, 9.2
void DeduplicationStore::MarkProcessed(int64_t update_id) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  // 更新水位线
  if (update_id > watermark_) {
    watermark_ = update_id;
  }

  // 添加到已处理集合
  processed_ids_.insert(update_id);

  logger_->debug("Marked update as processed: update_id={}", update_id);
}

// 获取当前水位线
// Requirements: 9.2
int64_t DeduplicationStore::GetWatermark() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return watermark_;
}

// 设置水位线
// Requirements: 9.2
void DeduplicationStore::SetWatermark(int64_t watermark) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  watermark_ = watermark;
  logger_->info("Set watermark: {}", watermark);
}

// 清理过期记录
// Requirements: 9.7
void DeduplicationStore::Cleanup(std::chrono::hours retention) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  // 计算保留阈值（水位线 - 估计的 7 天更新数）
  // 假设每天最多 10000 个更新
  int64_t retention_threshold = watermark_ - (retention.count() / 24 * 10000);

  // 移除小于阈值的 ID
  auto it = processed_ids_.begin();
  while (it != processed_ids_.end() && *it < retention_threshold) {
    it = processed_ids_.erase(it);
  }

  logger_->info("Cleaned up old dedup entries: threshold={}, remaining={}",
                retention_threshold, processed_ids_.size());
}

// 持久化到磁盘
// Requirements: 9.8
void DeduplicationStore::Save() {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  nlohmann::json j;
  j["watermark"] = watermark_;
  j["processed_ids"] = nlohmann::json::array();

  for (int64_t id : processed_ids_) {
    j["processed_ids"].push_back(id);
  }

  lock.unlock();

  // 写入文件
  std::ofstream file(store_path_);
  if (!file.is_open()) {
    logger_->error("Failed to save dedup store: {}", store_path_.string());
    return;
  }

  file << j.dump(2);
  file.close();

  logger_->debug("Saved dedup store: {}", store_path_.string());
}

// 从磁盘加载
// Requirements: 9.8
void DeduplicationStore::Load() {
  if (!std::filesystem::exists(store_path_)) {
    logger_->info("Dedup store not found, starting fresh: {}",
                  store_path_.string());
    return;
  }

  std::ifstream file(store_path_);
  if (!file.is_open()) {
    logger_->error("Failed to load dedup store: {}", store_path_.string());
    return;
  }

  nlohmann::json j;
  try {
    file >> j;
  } catch (const std::exception& e) {
    logger_->error("Failed to parse dedup store: {}", e.what());
    return;
  }

  std::unique_lock<std::shared_mutex> lock(mutex_);

  watermark_ = j.value("watermark", 0);
  processed_ids_.clear();

  if (j.contains("processed_ids") && j["processed_ids"].is_array()) {
    for (const auto& id : j["processed_ids"]) {
      processed_ids_.insert(id.get<int64_t>());
    }
  }

  logger_->info("Loaded dedup store: watermark={}, entries={}",
                watermark_, processed_ids_.size());
}

}  // namespace quantclaw
