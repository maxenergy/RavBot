// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

// Mirrors OpenClaw: src/agents/pi-embedded-helpers/turns.ts
// Implements validateAnthropicTurns and validateGeminiTurns logic in C++.

#include "quantclaw/core/turn_validator.hpp"

#include <set>
#include <string>
#include <vector>

namespace quantclaw {

TurnValidator::TurnValidator(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
  // Register built-in validators
  validators_["anthropic"] = &TurnValidator::FixAnthropicTurns;
  validators_["google"] = &TurnValidator::FixGoogleTurns;
  validators_["gemini"] = &TurnValidator::FixGoogleTurns;
}

std::vector<Message> TurnValidator::ValidateAndFix(
    const std::vector<Message>& messages,
    const std::string& provider_id) const {
  auto it = validators_.find(provider_id);
  if (it == validators_.end()) {
    return messages;  // No validator registered, return as-is
  }

  auto before_size = messages.size();
  auto result = it->second(messages);
  auto after_size = result.size();

  if (logger_ && before_size != after_size) {
    logger_->info(
        "TurnValidator[{}]: fixed message sequence {} -> {} messages",
        provider_id, before_size, after_size);
  }

  return result;
}

void TurnValidator::RegisterValidator(const std::string& provider_id,
                                      ProviderValidator validator) {
  validators_[provider_id] = std::move(validator);
}

// static
std::vector<Message> TurnValidator::FixAnthropicTurns(
    const std::vector<Message>& messages) {
  // Step 1: strip dangling tool_use blocks (mirrors stripDanglingAnthropicToolUses)
  auto stripped = strip_dangling_tool_uses(messages);

  // Step 2: merge consecutive user messages (Anthropic requires alternation)
  return merge_consecutive_role(stripped, "user");
}

// static
std::vector<Message> TurnValidator::FixGoogleTurns(
    const std::vector<Message>& messages) {
  // Gemini requires merging consecutive assistant messages
  return merge_consecutive_role(messages, "assistant");
}

// static
std::vector<Message> TurnValidator::strip_dangling_tool_uses(
    const std::vector<Message>& messages) {
  std::vector<Message> result;
  result.reserve(messages.size());

  for (size_t i = 0; i < messages.size(); ++i) {
    const auto& msg = messages[i];

    if (msg.role != "assistant") {
      result.push_back(msg);
      continue;
    }

    // Check if next message is a user message with tool_result blocks
    bool next_is_user = (i + 1 < messages.size()) &&
                        messages[i + 1].role == "user";

    if (!next_is_user) {
      result.push_back(msg);
      continue;
    }

    // Collect valid tool_use_ids from the next user message's tool_result blocks
    std::set<std::string> valid_tool_use_ids;
    for (const auto& block : messages[i + 1].content) {
      if (block.type == "tool_result" && !block.tool_use_id.empty()) {
        valid_tool_use_ids.insert(block.tool_use_id);
      }
    }

    // Filter out tool_use blocks that have no matching tool_result
    bool has_tool_use = false;
    for (const auto& block : msg.content) {
      if (block.type == "tool_use") {
        has_tool_use = true;
        break;
      }
    }

    if (!has_tool_use) {
      result.push_back(msg);
      continue;
    }

    // Build filtered content
    std::vector<ContentBlock> filtered;
    for (const auto& block : msg.content) {
      if (block.type != "tool_use") {
        filtered.push_back(block);
        continue;
      }
      // Keep tool_use only if its id has a matching tool_result
      if (valid_tool_use_ids.count(block.id) > 0) {
        filtered.push_back(block);
      }
    }

    if (filtered.empty() && !msg.content.empty()) {
      // All content removed — insert minimal fallback text block
      Message fallback;
      fallback.role = msg.role;
      fallback.content.push_back(
          ContentBlock::MakeText("[tool calls omitted]"));
      result.push_back(std::move(fallback));
    } else {
      Message fixed;
      fixed.role = msg.role;
      fixed.content = std::move(filtered);
      result.push_back(std::move(fixed));
    }
  }

  return result;
}

// static
std::vector<Message> TurnValidator::merge_consecutive_role(
    const std::vector<Message>& messages,
    const std::string& role) {
  if (messages.empty()) return messages;

  std::vector<Message> result;
  result.reserve(messages.size());

  for (const auto& msg : messages) {
    if (!result.empty() && result.back().role == role && msg.role == role) {
      // Merge: append content of current message into last
      for (const auto& block : msg.content) {
        result.back().content.push_back(block);
      }
    } else {
      result.push_back(msg);
    }
  }

  return result;
}

}  // namespace quantclaw
