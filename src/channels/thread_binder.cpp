// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/channels/thread_binder.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

namespace quantclaw {

ThreadBinder::ThreadBinder(const std::filesystem::path& store_path,
                             std::shared_ptr<spdlog::logger> logger)
    : store_path_(store_path), logger_(std::move(logger)) {
  // 确保目录存在
  if (!store_path_.parent_path().empty()) {
    std::filesystem::create_directories(store_path_.parent_path());
  }
}

// 获取线程对应的会话键
// Requirements: 10.1, 10.2
std::string ThreadBinder::GetSessionKey(
    const std::string& chat_id,
    const std::optional<std::string>& thread_id) const {
  // 如果没有 thread_id，返回默认会话键
  if (!thread_id) {
    return "agent:main:telegram:" + chat_id;
  }

  std::string key = make_key(chat_id, *thread_id);

  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = bindings_.find(key);
  if (it != bindings_.end()) {
    return it->second.session_key;
  }

  // 未绑定，返回默认会话键（包含 thread_id）
  return "agent:main:telegram:" + chat_id + ":" + *thread_id;
}

// 绑定线程到会话
// Requirements: 10.3, 10.4
void ThreadBinder::BindThread(const std::string& chat_id,
                                const std::string& thread_id,
                                const std::string& session_key) {
  std::string key = make_key(chat_id, thread_id);

  std::unique_lock<std::shared_mutex> lock(mutex_);

  ThreadBinding binding;
  binding.chat_id = chat_id;
  binding.thread_id = thread_id;
  binding.session_key = session_key;
  binding.created_at = get_timestamp();

  bindings_[key] = binding;

  logger_->info("Bound thread to session: chat={}, thread={}, session={}",
                chat_id, thread_id, session_key);
}

// 解绑线程
// Requirements: 10.5
void ThreadBinder::UnbindThread(const std::string& chat_id,
                                 const std::string& thread_id) {
  std::string key = make_key(chat_id, thread_id);

  std::unique_lock<std::shared_mutex> lock(mutex_);
  auto it = bindings_.find(key);
  if (it != bindings_.end()) {
    logger_->info("Unbound thread: chat={}, thread={}", chat_id, thread_id);
    bindings_.erase(it);
  }
}

// 列出聊天的所有线程
// Requirements: 10.6
std::vector<ThreadBinding> ThreadBinder::ListThreads(
    const std::string& chat_id) const {
  std::vector<ThreadBinding> result;

  std::shared_lock<std::shared_mutex> lock(mutex_);
  for (const auto& [key, binding] : bindings_) {
    if (binding.chat_id == chat_id) {
      result.push_back(binding);
    }
  }

  return result;
}

// 持久化到磁盘
// Requirements: 10.8
void ThreadBinder::Save() {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  nlohmann::json j = nlohmann::json::array();

  for (const auto& [key, binding] : bindings_) {
    nlohmann::json item;
    item["chatId"] = binding.chat_id;
    item["threadId"] = binding.thread_id;
    item["sessionKey"] = binding.session_key;
    item["createdAt"] = binding.created_at;
    j.push_back(item);
  }

  lock.unlock();

  // 写入文件
  std::ofstream file(store_path_);
  if (!file.is_open()) {
    logger_->error("Failed to save thread bindings: {}", store_path_.string());
    return;
  }

  file << j.dump(2);
  file.close();

  logger_->debug("Saved thread bindings: {}", store_path_.string());
}

// 从磁盘加载
// Requirements: 10.8
void ThreadBinder::Load() {
  if (!std::filesystem::exists(store_path_)) {
    logger_->info("Thread bindings not found, starting fresh: {}",
                  store_path_.string());
    return;
  }

  std::ifstream file(store_path_);
  if (!file.is_open()) {
    logger_->error("Failed to load thread bindings: {}", store_path_.string());
    return;
  }

  nlohmann::json j;
  try {
    file >> j;
  } catch (const std::exception& e) {
    logger_->error("Failed to parse thread bindings: {}", e.what());
    return;
  }

  std::unique_lock<std::shared_mutex> lock(mutex_);
  bindings_.clear();

  if (j.is_array()) {
    for (const auto& item : j) {
      ThreadBinding binding;
      binding.chat_id = item.value("chatId", "");
      binding.thread_id = item.value("threadId", "");
      binding.session_key = item.value("sessionKey", "");
      binding.created_at = item.value("createdAt", "");

      if (!binding.chat_id.empty() && !binding.thread_id.empty()) {
        std::string key = make_key(binding.chat_id, binding.thread_id);
        bindings_[key] = binding;
      }
    }
  }

  logger_->info("Loaded thread bindings: count={}", bindings_.size());
}

// 生成键
std::string ThreadBinder::make_key(const std::string& chat_id,
                                    const std::string& thread_id) const {
  return chat_id + ":" + thread_id;
}

// 获取时间戳
std::string ThreadBinder::get_timestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
#ifdef _WIN32
  gmtime_s(&tm, &time_t);
#else
  gmtime_r(&time_t, &tm);
#endif
  std::ostringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return ss.str();
}

}  // namespace quantclaw
