// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "quantclaw/providers/llm_provider.hpp"

namespace quantclaw {

// Function type for provider-specific turn validators.
// Takes a message sequence and returns a fixed sequence.
using ProviderValidator =
    std::function<std::vector<Message>(const std::vector<Message>&)>;

// Validates and fixes provider-specific message turn sequences.
//
// Different LLM providers have different requirements for message ordering:
// - Anthropic: strict user/assistant alternation, user must be first
// - Google/Gemini: consecutive assistant messages must be merged
//
// Mirrors OpenClaw's turns.ts validateAnthropicTurns/validateGeminiTurns.
class TurnValidator {
 public:
  explicit TurnValidator(std::shared_ptr<spdlog::logger> logger = nullptr);

  // Validate and fix message sequence for a given provider.
  // Applies the registered validator for the provider, or returns
  // messages unchanged if no validator is registered.
  std::vector<Message> ValidateAndFix(
      const std::vector<Message>& messages,
      const std::string& provider_id) const;

  // Register a custom validator for a provider.
  // Allows extending validation to new providers without modifying this class.
  void RegisterValidator(const std::string& provider_id,
                         ProviderValidator validator);

  // Static helpers — also usable directly without an instance.

  // Fix Anthropic turn sequence:
  // - Strips dangling tool_use blocks from assistant messages that lack
  //   matching tool_result in the following user message.
  // - Merges consecutive user messages.
  // Mirrors OpenClaw validateAnthropicTurns().
  static std::vector<Message> FixAnthropicTurns(
      const std::vector<Message>& messages);

  // Fix Google/Gemini turn sequence:
  // - Merges consecutive assistant messages.
  // Mirrors OpenClaw validateGeminiTurns().
  static std::vector<Message> FixGoogleTurns(
      const std::vector<Message>& messages);

 private:
  // Strip dangling tool_use blocks from assistant messages when the
  // immediately following user message has no matching tool_result.
  static std::vector<Message> strip_dangling_tool_uses(
      const std::vector<Message>& messages);

  // Merge consecutive messages of the given role by concatenating content.
  static std::vector<Message> merge_consecutive_role(
      const std::vector<Message>& messages,
      const std::string& role);

  std::map<std::string, ProviderValidator> validators_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw
