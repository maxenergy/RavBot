// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#include "quantclaw/providers/cooldown_tracker.hpp"
#include "quantclaw/providers/provider_error.hpp"

namespace quantclaw {

class ProviderRegistry;
class LLMProvider;
struct ModelRef;

// 重试配置：定义重试策略和指数退避参数
// Requirements: 5.2, 5.3
struct RetryConfig {
  int max_retries = 3;              // 最大重试次数（默认 3 次）
  int initial_backoff_ms = 1000;    // 初始退避时间（默认 1 秒）
  double backoff_multiplier = 2.0;  // 退避倍数（默认 2.0，即 1s、2s、4s）
  int max_backoff_ms = 60000;       // 最大退避时间（默认 60 秒）
};

// Auth profile: one API key for a provider.
// A provider may have multiple profiles for key rotation.
struct AuthProfile {
  std::string id;           // e.g. "prod", "backup"
  std::string api_key;      // Direct key value
  std::string api_key_env;  // Env var name (resolved at startup)
};

// 故障转移统计信息
// Requirements: 2.6
struct FailoverStats {
  int total_requests = 0;        // 总请求数
  int successful_requests = 0;   // 成功请求数
  int failed_requests = 0;       // 失败请求数
  int failover_count = 0;        // 故障转移次数

  // 每个提供商的使用统计：provider_id -> 使用次数
  std::unordered_map<std::string, int> provider_usage;

  // 每个配置的使用统计：provider_id:profile_id -> 使用次数
  std::unordered_map<std::string, int> profile_usage;
};

// Result of a failover resolution attempt.
struct ResolvedProvider {
  std::shared_ptr<LLMProvider> provider;
  std::string provider_id;
  std::string profile_id;
  std::string model;
  bool is_fallback = false;  // True if this is not the primary model
};

// Orchestrates multi-profile key rotation and model fallback chains.
//
// Resolution algorithm:
// 1. For the requested model, find its provider
// 2. Check session pin → use pinned profile if still healthy
// 3. Iterate provider's auth profiles, skip any in cooldown
// 4. If all profiles are in cooldown, try next model in fallback chain
// 5. If all models exhausted, return nullopt
//
// After a successful API call, the caller should call RecordSuccess().
// After a failed API call, the caller should call RecordFailure().
class FailoverResolver {
 public:
  FailoverResolver(ProviderRegistry* registry,
                   std::shared_ptr<spdlog::logger> logger);

  // Set the fallback model chain (in priority order).
  // Example: ["anthropic/claude-sonnet-4-6", "openai/gpt-4o"]
  void SetFallbackChain(const std::vector<std::string>& models);

  // Set auth profiles for a provider.
  void SetProfiles(const std::string& provider_id,
                   const std::vector<AuthProfile>& profiles);

  // Resolve a provider for the given model, considering cooldowns
  // and session pin. Returns nullopt if all options exhausted.
  std::optional<ResolvedProvider> Resolve(
      const std::string& model,
      const std::string& session_key = "");

  // Record a successful API call. Clears cooldown, pins session.
  void RecordSuccess(const std::string& provider_id,
                     const std::string& profile_id,
                     const std::string& session_key = "");

  // Record a failed API call. Sets cooldown based on error kind.
  // If retry_after_seconds > 0, uses that as cooldown instead of backoff.
  void RecordFailure(const std::string& provider_id,
                     const std::string& profile_id,
                     ProviderErrorKind kind,
                     int retry_after_seconds = 0);

  // Clear session pin (e.g., on session reset).
  void ClearSessionPin(const std::string& session_key);

  // Get cooldown tracker (for status queries).
  const CooldownTracker& GetCooldownTracker() const { return cooldown_; }

  // Get failover statistics
  // Requirements: 2.6
  FailoverStats GetStats() const;

  // Set retry configuration at runtime
  // Requirements: 5.2
  void SetRetryConfig(const RetryConfig& config);

  // Reset all cooldowns (for testing and admin operations)
  // Requirements: 2.1, 2.2, 2.3
  void ResetCooldowns();

 private:
  std::string cooldown_key(const std::string& provider_id,
                           const std::string& profile_id) const;

  // Try to resolve a specific model (without walking fallback chain).
  std::optional<ResolvedProvider> try_resolve_model(
      const std::string& model,
      const std::string& session_key);

  ProviderRegistry* registry_;
  std::shared_ptr<spdlog::logger> logger_;
  CooldownTracker cooldown_;

  mutable std::mutex mu_;
  std::vector<std::string> fallback_chain_;

  // provider_id → auth profiles
  std::unordered_map<std::string, std::vector<AuthProfile>> profiles_;

  // session_key → {provider_id, profile_id}
  struct SessionPin {
    std::string provider_id;
    std::string profile_id;
  };
  std::unordered_map<std::string, SessionPin> session_pins_;

  // Failover statistics (mutable for const GetStats())
  mutable FailoverStats stats_;

  // Retry configuration
  RetryConfig retry_config_;
};

}  // namespace quantclaw
