// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "ravbot/providers/llm_provider.hpp"

namespace ravbot {

// Tracks cumulative token usage per session and globally.
// Aligns with OpenClaw UsageAccumulator, including cache read/write fields.
// Thread-safe: all methods can be called from any thread.
class UsageAccumulator {
 public:
  struct Stats {
    // Cumulative counters
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;
    int64_t cache_read_tokens = 0;   // Anthropic cache_read_input_tokens
    int64_t cache_write_tokens = 0;  // Anthropic cache_creation_input_tokens
    int64_t total_tokens = 0;
    int turns = 0;  // Number of LLM calls

    // Last-call values (not cumulative, mirrors OpenClaw lastCacheRead etc.)
    int64_t last_input_tokens = 0;
    int64_t last_output_tokens = 0;
    int64_t last_cache_read_tokens = 0;
    int64_t last_cache_write_tokens = 0;
  };

  // Record a single LLM call's token usage (accepts full TokenUsage struct).
  void Record(const std::string& session_key, const TokenUsage& usage);

  // Legacy overload for backward compatibility.
  void Record(const std::string& session_key,
              int input_tokens, int output_tokens);

  // Get usage for a specific session.
  Stats GetSession(const std::string& session_key) const;

  // Get global (all sessions) usage.
  Stats GetGlobal() const;

  // Reset usage for a specific session.
  void ResetSession(const std::string& session_key);

  // Reset all usage.
  void ResetAll();

  // Serialize to JSON (includes cacheReadTokens/cacheWriteTokens fields).
  nlohmann::json ToJson() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Stats> sessions_;
  Stats global_;
};

}  // namespace ravbot
