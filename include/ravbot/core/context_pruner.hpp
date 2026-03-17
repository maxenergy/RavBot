// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "ravbot/providers/llm_provider.hpp"

namespace ravbot {

// Compression strategy for message history pruning.
// Defines which message types to preserve during compression.
struct CompressionStrategy {
  bool preserve_recent = true;       // Keep recent messages
  bool preserve_tool_calls = true;   // Keep tool calls and results
  bool preserve_system = true;       // Keep system messages
  int min_messages = 5;              // Minimum messages to keep
};

// Statistics recorded during a compression operation.
// Satisfies Requirement 3.7: record messages removed and tokens saved.
struct CompressionStats {
  int messages_removed = 0;  // Number of messages dropped from history
  int tokens_saved = 0;      // Estimated tokens freed by compression
};

// Prunes old tool results from conversation history to reduce context size.
//
// Strategy:
// - Recent assistant messages (within protect_recent) keep their tool
//   results intact.
// - Older tool results are "soft pruned": truncated to a summary
//   showing the first/last few lines with an ellipsis marker.
// - Very old tool results (beyond hard_prune_after) are replaced
//   entirely with a placeholder.
//
// This does NOT modify the original history — it returns a new copy.
class ContextPruner {
 public:
  struct Options {
    int protect_recent = 3;      // Keep tool results for the N most recent
                                 // assistant messages intact
    int soft_prune_lines = 5;    // Keep this many lines at start+end for soft
    int hard_prune_after = 10;   // Hard prune tool results older than this
                                 // many assistant messages
    int max_tool_result_chars = 2000;  // Soft prune results exceeding this
    int context_window = 0;      // If > 0, use budget-based pruning
    int max_tokens = 8192;       // Model max_tokens (output budget)
    double prune_target_ratio = 0.75;  // Target: use at most this fraction of window
  };

  // Prune tool results in a message history.
  // Returns a new vector with pruned content — does not modify input.
  static std::vector<Message> Prune(const std::vector<Message>& history,
                                    const Options& opts);

  // Estimate token count for a message (rough: 4 chars ≈ 1 token)
  static int EstimateTokens(const Message& msg);
  static int EstimateTokens(const std::vector<Message>& msgs);

  // Compress messages to fit target token count using CompressionStrategy.
  // This is a higher-level interface that uses the Options-based Prune method.
  static std::vector<Message> Compress(
      const std::vector<Message>& messages,
      int target_tokens,
      const CompressionStrategy& strategy = {});

  // Compress messages and populate stats with messages_removed and
  // tokens_saved. Satisfies Requirement 3.7.
  static std::vector<Message> CompressWithStats(
      const std::vector<Message>& messages,
      int target_tokens,
      CompressionStats& stats,
      const CompressionStrategy& strategy = {});

  // Truncate long tool results to a maximum character count.
  // Applies content-aware tail preservation and an absolute hard limit.
  static std::string TruncateToolResult(
      const std::string& result,
      int max_chars = 10000);

  // Absolute hard limit for tool results (400,000 chars).
  // Applied before max_chars to prevent unbounded context growth.
  static constexpr int kHardMaxToolResultChars = 400000;

 private:
  // Soft-prune a tool result: keep first/last N lines with ellipsis
  static std::string soft_prune(const std::string& content, int keep_lines);

  // Content-aware tail importance detector.
  // Returns true if the last 2000 chars of content contain error traces,
  // stack traces, exception messages, or JSON endings — content worth keeping.
  static bool has_important_tail(const std::string& content);
};

}  // namespace ravbot
