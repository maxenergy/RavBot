// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <set>
#include <shared_mutex>
#include <memory>
#include <spdlog/spdlog.h>

namespace ravbot {

// 去重存储：持久化存储已处理的 update_id，防止重复处理
// Requirements: 9.1, 9.2, 9.3, 9.7, 9.8
class DeduplicationStore {
 public:
  explicit DeduplicationStore(const std::filesystem::path& store_path,
                               std::shared_ptr<spdlog::logger> logger);

  // 检查 update_id 是否已处理
  // Requirements: 9.1, 9.3
  bool IsProcessed(int64_t update_id) const;

  // 标记 update_id 为已处理
  // Requirements: 9.1, 9.2
  void MarkProcessed(int64_t update_id);

  // 获取当前水位线（最高已处理 update_id）
  // Requirements: 9.2
  int64_t GetWatermark() const;

  // 设置水位线
  // Requirements: 9.2
  void SetWatermark(int64_t watermark);

  // 清理过期记录（保留最近 7 天）
  // Requirements: 9.7
  void Cleanup(std::chrono::hours retention = std::chrono::hours(168));

  // 持久化到磁盘
  // Requirements: 9.8
  void Save();

  // 从磁盘加载
  // Requirements: 9.8
  void Load();

 private:
  std::filesystem::path store_path_;
  std::shared_ptr<spdlog::logger> logger_;
  int64_t watermark_ = 0;
  std::set<int64_t> processed_ids_;
  mutable std::shared_mutex mutex_;
};

}  // namespace ravbot
