// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

namespace ravbot {

// 线程绑定信息
// Requirements: 10.1, 10.2
struct ThreadBinding {
  std::string chat_id;
  std::string thread_id;
  std::string session_key;
  std::string created_at;
};

// 线程绑定器：将 Telegram 线程 ID 映射到独立的会话
// Requirements: 10.1, 10.2, 10.3, 10.4, 10.5, 10.6, 10.7
class ThreadBinder {
 public:
  ThreadBinder(const std::filesystem::path& store_path,
               std::shared_ptr<spdlog::logger> logger);

  // 获取线程对应的会话键
  // Requirements: 10.1, 10.2
  std::string GetSessionKey(const std::string& chat_id,
                             const std::optional<std::string>& thread_id) const;

  // 绑定线程到会话
  // Requirements: 10.3, 10.4
  void BindThread(const std::string& chat_id, const std::string& thread_id,
                  const std::string& session_key);

  // 解绑线程（当线程被删除时）
  // Requirements: 10.5
  void UnbindThread(const std::string& chat_id, const std::string& thread_id);

  // 列出聊天的所有线程
  // Requirements: 10.6
  std::vector<ThreadBinding> ListThreads(const std::string& chat_id) const;

  // 持久化到磁盘
  // Requirements: 10.8
  void Save();

  // 从磁盘加载
  // Requirements: 10.8
  void Load();

 private:
  std::string make_key(const std::string& chat_id,
                       const std::string& thread_id) const;
  std::string get_timestamp() const;

  std::filesystem::path store_path_;
  std::shared_ptr<spdlog::logger> logger_;

  // (chat_id + ":" + thread_id) -> session_key
  std::map<std::string, ThreadBinding> bindings_;
  mutable std::shared_mutex mutex_;
};

}  // namespace ravbot
