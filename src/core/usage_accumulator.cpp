// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/core/usage_accumulator.hpp"

namespace ravbot {

// Record a single LLM call's token usage — full TokenUsage struct.
// Aligns with OpenClaw UsageAccumulator including cache read/write fields.
void UsageAccumulator::Record(const std::string& session_key,
                              const TokenUsage& usage) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& s = sessions_[session_key];

  // Update cumulative counters
  s.input_tokens += usage.prompt_tokens;
  s.output_tokens += usage.completion_tokens;
  s.cache_read_tokens += usage.cache_read_input_tokens;
  s.cache_write_tokens += usage.cache_creation_input_tokens;
  s.total_tokens += usage.total_tokens > 0
                        ? usage.total_tokens
                        : (usage.prompt_tokens + usage.completion_tokens);
  s.turns++;

  // Update last-call values (not cumulative)
  s.last_input_tokens = usage.prompt_tokens;
  s.last_output_tokens = usage.completion_tokens;
  s.last_cache_read_tokens = usage.cache_read_input_tokens;
  s.last_cache_write_tokens = usage.cache_creation_input_tokens;

  // Update global stats
  global_.input_tokens += usage.prompt_tokens;
  global_.output_tokens += usage.completion_tokens;
  global_.cache_read_tokens += usage.cache_read_input_tokens;
  global_.cache_write_tokens += usage.cache_creation_input_tokens;
  global_.total_tokens += usage.total_tokens > 0
                              ? usage.total_tokens
                              : (usage.prompt_tokens + usage.completion_tokens);
  global_.turns++;
  global_.last_input_tokens = usage.prompt_tokens;
  global_.last_output_tokens = usage.completion_tokens;
  global_.last_cache_read_tokens = usage.cache_read_input_tokens;
  global_.last_cache_write_tokens = usage.cache_creation_input_tokens;
}

// Legacy overload: backward compatibility for callers passing raw ints.
void UsageAccumulator::Record(const std::string& session_key,
                              int input_tokens, int output_tokens) {
  TokenUsage usage;
  usage.prompt_tokens = input_tokens;
  usage.completion_tokens = output_tokens;
  usage.total_tokens = input_tokens + output_tokens;
  Record(session_key, usage);
}

UsageAccumulator::Stats UsageAccumulator::GetSession(
    const std::string& session_key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_key);
  if (it == sessions_.end()) return {};
  return it->second;
}

UsageAccumulator::Stats UsageAccumulator::GetGlobal() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return global_;
}

void UsageAccumulator::ResetSession(const std::string& session_key) {
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.erase(session_key);
}

void UsageAccumulator::ResetAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.clear();
  global_ = {};
}

nlohmann::json UsageAccumulator::ToJson() const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto stats_to_json = [](const Stats& s) -> nlohmann::json {
    return {{"inputTokens", s.input_tokens},
            {"outputTokens", s.output_tokens},
            {"cacheReadTokens", s.cache_read_tokens},
            {"cacheWriteTokens", s.cache_write_tokens},
            {"totalTokens", s.total_tokens},
            {"turns", s.turns},
            {"lastInputTokens", s.last_input_tokens},
            {"lastOutputTokens", s.last_output_tokens},
            {"lastCacheReadTokens", s.last_cache_read_tokens},
            {"lastCacheWriteTokens", s.last_cache_write_tokens}};
  };

  nlohmann::json j;
  j["global"] = stats_to_json(global_);
  nlohmann::json sessions = nlohmann::json::object();
  for (const auto& [key, s] : sessions_) {
    sessions[key] = stats_to_json(s);
  }
  j["sessions"] = sessions;
  return j;
}

}  // namespace ravbot
