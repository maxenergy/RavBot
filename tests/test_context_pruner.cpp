// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

// Tests for ContextPruner: content-aware truncation, hard limit, and improved
// truncation notice. Aligns with the P1.3 plan additions.

#include "ravbot/core/context_pruner.hpp"

#include <string>

#include <gtest/gtest.h>

namespace ravbot {
namespace {

// ── TruncateToolResult: basic behaviour ──────────────────────────────────────

TEST(ContextPrunerTruncate, ShortResultPassedThrough) {
  const std::string short_result = "ok";
  EXPECT_EQ(ContextPruner::TruncateToolResult(short_result, 100), short_result);
}

TEST(ContextPrunerTruncate, ExactLimitPassedThrough) {
  const std::string result(100, 'x');
  EXPECT_EQ(ContextPruner::TruncateToolResult(result, 100), result);
}

TEST(ContextPrunerTruncate, LongResultTruncated) {
  // Build a multi-line result that exceeds max_chars
  std::string result;
  for (int i = 0; i < 200; ++i) {
    result += "line " + std::to_string(i) + "\n";
  }
  const int max_chars = 500;
  auto truncated = ContextPruner::TruncateToolResult(result, max_chars);
  EXPECT_LT(static_cast<int>(truncated.size()), static_cast<int>(result.size()));
}

// ── Improved truncation notice ────────────────────────────────────────────────

TEST(ContextPrunerTruncate, TruncationNoticePresent) {
  std::string result;
  for (int i = 0; i < 500; ++i) result += "data line\n";
  auto truncated = ContextPruner::TruncateToolResult(result, 200);
  // New notice format
  EXPECT_NE(truncated.find("Content truncated"), std::string::npos);
  EXPECT_NE(truncated.find("offset/limit"), std::string::npos);
}

TEST(ContextPrunerTruncate, TruncationNoticeIncludesOriginalSize) {
  std::string result(2000, 'a');
  for (int i = 0; i < 24; ++i) result[i * 80 + 79] = '\n';  // add newlines
  auto truncated = ContextPruner::TruncateToolResult(result, 100);
  // The notice should mention the original character count
  EXPECT_NE(truncated.find("2000"), std::string::npos);
}

// ── has_important_tail detection ─────────────────────────────────────────────
// has_important_tail is private, but we test it indirectly:
// when the tail contains error patterns the function uses head+tail strategy
// which preserves lines from BOTH ends of the content.

TEST(ContextPrunerTruncate, TailWithErrorPreservesEndContent) {
  // Build content where tail contains "Error:" — should preserve tail lines
  std::string body;
  for (int i = 0; i < 300; ++i) {
    body += "normal line " + std::to_string(i) + "\n";
  }
  const std::string tail_error = "Error: something went wrong\n";
  const std::string content = body + tail_error;

  auto truncated =
      ContextPruner::TruncateToolResult(content, 500);

  // With content-aware tail preservation the error line should appear in output
  EXPECT_NE(truncated.find("Error: something went wrong"), std::string::npos);
}

TEST(ContextPrunerTruncate, TailWithJsonEndingPreservesTail) {
  // Tail ends with JSON — should also trigger head+tail strategy
  std::string body;
  for (int i = 0; i < 300; ++i) body += "data: " + std::to_string(i) + "\n";
  const std::string tail_json = "{\"status\": \"ok\"}";
  const std::string content = body + tail_json;

  auto truncated = ContextPruner::TruncateToolResult(content, 500);
  EXPECT_NE(truncated.find("{\"status\""), std::string::npos);
}

TEST(ContextPrunerTruncate, TailWithExceptionPreservesTail) {
  std::string body;
  for (int i = 0; i < 300; ++i) body += "step " + std::to_string(i) + "\n";
  body += "Traceback (most recent call last):\n  File test.py\nValueError: bad";
  auto truncated = ContextPruner::TruncateToolResult(body, 500);
  EXPECT_NE(truncated.find("ValueError"), std::string::npos);
}

// ── kHardMaxToolResultChars absolute hard limit ───────────────────────────────

TEST(ContextPrunerTruncate, HardLimitConstantIs400K) {
  EXPECT_EQ(ContextPruner::kHardMaxToolResultChars, 400000);
}

TEST(ContextPrunerTruncate, HardLimitApplied) {
  // Build content exceeding 400K chars
  const int over_limit = ContextPruner::kHardMaxToolResultChars + 50000;
  std::string huge(over_limit, 'x');
  // Add newlines so it's not just one line
  for (int i = 0; i < over_limit / 80; ++i) huge[i * 80] = '\n';

  auto truncated = ContextPruner::TruncateToolResult(huge, 10000);
  // Must be significantly shorter than input
  EXPECT_LT(static_cast<int>(truncated.size()), over_limit);
  // Must still contain the truncation notice
  EXPECT_NE(truncated.find("Content truncated"), std::string::npos);
}

TEST(ContextPrunerTruncate, HardLimitNotAppliedWhenUnderLimit) {
  // 1000 chars should pass through unmodified with large max_chars
  const std::string small(1000, 'y');
  EXPECT_EQ(ContextPruner::TruncateToolResult(small, 10000), small);
}

}  // namespace
}  // namespace ravbot
