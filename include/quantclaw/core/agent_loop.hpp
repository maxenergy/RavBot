// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/common/noncopyable.hpp"
#include "quantclaw/config.hpp"
#include "quantclaw/core/context_pruner.hpp"
#include "quantclaw/core/turn_validator.hpp"
#include "quantclaw/core/usage_accumulator.hpp"
#include "quantclaw/providers/llm_provider.hpp"

namespace quantclaw {

class MemoryManager;
class SkillLoader;
class ToolRegistry;
class ProviderRegistry;
class SubagentManager;
class FailoverResolver;

// --- Agent Event (for streaming) ---

struct AgentEvent {
  std::string
      type;  // "text_delta" | "tool_use" | "tool_result" | "message_end"
  nlohmann::json data;
};

using AgentEventCallback = std::function<void(const AgentEvent&)>;

// --- Agent Loop ---

class AgentLoop : public Noncopyable {
 public:
  AgentLoop(std::shared_ptr<MemoryManager> memory_manager,
            std::shared_ptr<SkillLoader> skill_loader,
            std::shared_ptr<ToolRegistry> tool_registry,
            std::shared_ptr<LLMProvider> llm_provider,
            const AgentConfig& agent_config,
            std::shared_ptr<spdlog::logger> logger);

  // Process a message with externally-provided history and system prompt.
  // Returns all new messages generated during the turn (assistant +
  // tool_result). usage_session_key: if non-empty, usage is recorded under this
  // key instead of session_key_ — allows per-request tracking without mutating
  // shared state.
  std::vector<Message>
  ProcessMessage(const std::string& message,
                 const std::vector<Message>& history,
                 const std::string& system_prompt,
                 const std::string& usage_session_key = "");

  // Streaming version — returns all new messages generated during the turn.
  // usage_session_key: same semantics as ProcessMessage.
  std::vector<Message> ProcessMessageStream(
      const std::string& message, const std::vector<Message>& history,
      const std::string& system_prompt, AgentEventCallback callback,
      const std::string& usage_session_key = "");

  // Stop the current agent turn
  void Stop();

  // Query whether an abort has been requested (thread-safe).
  bool IsAborted() const {
    return stop_requested_.load();
  }

  // Set max iterations
  void SetMaxIterations(int max) {
    max_iterations_ = max;
  }

  // Update agent config (for hot-reload)
  void SetConfig(const AgentConfig& config);

  // Get current config (for testing)
  const AgentConfig& GetConfig() const {
    return agent_config_;
  }

  // Set provider registry for dynamic model resolution
  void SetProviderRegistry(ProviderRegistry* registry) {
    provider_registry_ = registry;
  }

  // Set subagent manager for spawning child agents
  void SetSubagentManager(SubagentManager* manager) {
    subagent_manager_ = manager;
  }

  // Set failover resolver for multi-profile key rotation and model fallback
  void SetFailoverResolver(FailoverResolver* resolver) {
    failover_resolver_ = resolver;
  }

  // Set session key for failover session pinning
  void SetSessionKey(const std::string& key) {
    session_key_ = key;
  }

  // Set usage accumulator for token tracking
  void SetUsageAccumulator(std::shared_ptr<UsageAccumulator> acc) {
    usage_accumulator_ = acc;
  }

  // Get usage accumulator (may be null)
  std::shared_ptr<UsageAccumulator> GetUsageAccumulator() const {
    return usage_accumulator_;
  }

  // Set turn validator for provider-specific message sequence fixes
  void SetTurnValidator(std::shared_ptr<TurnValidator> validator) {
    turn_validator_ = validator;
  }

  // Set context pruner for context window protection
  void SetContextPruner(std::shared_ptr<ContextPruner> pruner) {
    context_pruner_ = std::move(pruner);
  }

  // Set embedding manager for automatic message indexing
  void SetEmbeddingManager(std::shared_ptr<class EmbeddingManager> manager) {
    embedding_manager_ = manager;
  }

  // Set model dynamically (resolves via ProviderRegistry if available)
  void SetModel(const std::string& model_ref);

 private:
  // Resolve current provider (from registry or fallback to injected provider)
  std::shared_ptr<LLMProvider> resolve_provider();
  std::string resolved_request_model() const;

  // Execute a single LLM API call with retry + failover.
  // Retries up to max_retries times with exponential backoff on the current
  // provider/profile. If all retries are exhausted, records failure with
  // FailoverResolver and re-resolves to the next available provider/profile
  // in the failover chain. Repeats until a response is obtained or all
  // failover options are exhausted.
  // Returns the LLM response on success; throws on unrecoverable failure.
  ChatCompletionResponse execute_with_retry(
      ChatCompletionRequest& request, std::shared_ptr<LLMProvider>& provider,
      const std::string& original_model, int max_retries = kDefaultMaxRetries);

  // Streaming variant of execute_with_retry.
  // Calls provider->ChatCompletionStream with the given callback.
  // Returns true on success, false if all failover options exhausted.
  bool execute_stream_with_retry(
      ChatCompletionRequest& request, std::shared_ptr<LLMProvider>& provider,
      const std::string& original_model,
      const std::function<void(const ChatCompletionResponse&)>& stream_cb,
      int max_retries = kDefaultMaxRetries);

  std::vector<std::string>
  handle_tool_calls(const std::vector<nlohmann::json>& tool_calls);

  // Sleep for the given duration, but wake early if stop_requested_ is set.
  // Returns true if the full duration elapsed, false if interrupted by abort.
  bool interruptible_sleep(std::chrono::seconds duration);

  std::shared_ptr<MemoryManager> memory_manager_;
  std::shared_ptr<SkillLoader> skill_loader_;
  std::shared_ptr<ToolRegistry> tool_registry_;
  std::shared_ptr<LLMProvider> llm_provider_;  // Fallback / injected provider
  ProviderRegistry* provider_registry_ = nullptr;        // Non-owning, optional
  SubagentManager* subagent_manager_ = nullptr;          // Non-owning, optional
  FailoverResolver* failover_resolver_ = nullptr;        // Non-owning, optional
  std::shared_ptr<UsageAccumulator> usage_accumulator_;  // Shared ownership
  std::shared_ptr<TurnValidator> turn_validator_;        // Shared ownership
  std::shared_ptr<ContextPruner> context_pruner_;        // Shared ownership
  std::shared_ptr<class EmbeddingManager> embedding_manager_;  // Shared ownership
  std::string session_key_;  // For failover session pinning
  std::shared_ptr<spdlog::logger> logger_;
  AgentConfig agent_config_;
  std::atomic<bool> stop_requested_{false};
  int max_iterations_ = 15;

  // Tracking last resolved provider/profile for failover reporting
  std::string last_provider_id_;
  std::string last_profile_id_;
  std::string resolved_model_name_;
};

}  // namespace quantclaw
