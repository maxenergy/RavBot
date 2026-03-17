// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

// Tests for TurnValidator P2.2 additions:
//   - timestamp preservation when merging consecutive messages
//   - null/empty content defensive skip

#include "ravbot/core/turn_validator.hpp"

#include <gtest/gtest.h>

namespace ravbot {
namespace {

// Utility: build a Message with role, single text block, and optional timestamp
static Message make_msg(const std::string& role,
                        const std::string& text,
                        const std::string& timestamp = "") {
  Message m(role, text);
  m.timestamp = timestamp;
  return m;
}

// ── timestamp preservation ────────────────────────────────────────────────────

// When two consecutive user messages are merged, the resulting message should
// carry the timestamp of the LATER (second) message if it is non-empty.
TEST(TurnValidatorTimestamp, MergePreservesLaterTimestamp) {
  std::vector<Message> msgs = {
      make_msg("user", "hello", "2025-01-01T00:00:00Z"),
      make_msg("user", "world", "2025-01-02T00:00:00Z"),
  };

  auto fixed = TurnValidator::FixAnthropicTurns(msgs);
  ASSERT_EQ(fixed.size(), 1u);
  EXPECT_EQ(fixed[0].timestamp, "2025-01-02T00:00:00Z");
}

// When the second message has an empty timestamp, keep the first message's
// timestamp (fallback behaviour).
TEST(TurnValidatorTimestamp, MergeFallsBackToPreviousTimestamp) {
  std::vector<Message> msgs = {
      make_msg("user", "first", "2025-01-01T00:00:00Z"),
      make_msg("user", "second", ""),
  };

  auto fixed = TurnValidator::FixAnthropicTurns(msgs);
  ASSERT_EQ(fixed.size(), 1u);
  EXPECT_EQ(fixed[0].timestamp, "2025-01-01T00:00:00Z");
}

// A single message retains its own timestamp unchanged.
TEST(TurnValidatorTimestamp, SingleMessageKeepsOwnTimestamp) {
  std::vector<Message> msgs = {
      make_msg("user", "hi", "2025-06-15T12:00:00Z"),
  };

  auto fixed = TurnValidator::FixAnthropicTurns(msgs);
  ASSERT_EQ(fixed.size(), 1u);
  EXPECT_EQ(fixed[0].timestamp, "2025-06-15T12:00:00Z");
}

// Three consecutive user messages: final result should have the timestamp of
// the last non-empty-timestamp message in the chain.
TEST(TurnValidatorTimestamp, ThreeConsecutiveUserMsgs) {
  std::vector<Message> msgs = {
      make_msg("user", "a", "t1"),
      make_msg("user", "b", "t2"),
      make_msg("user", "c", "t3"),
  };

  auto fixed = TurnValidator::FixAnthropicTurns(msgs);
  ASSERT_EQ(fixed.size(), 1u);
  EXPECT_EQ(fixed[0].timestamp, "t3");
}

// ── empty/null content defensive skip ────────────────────────────────────────

// Messages with empty content should be silently dropped by merge_consecutive_role.
TEST(TurnValidatorEmptyContent, EmptyContentMessageDropped) {
  // Build a user message with no content blocks
  Message empty_msg;
  empty_msg.role = "user";
  // content is default-empty

  std::vector<Message> msgs = {
      make_msg("assistant", "hello"),
      empty_msg,
      make_msg("user", "real content"),
  };

  auto fixed = TurnValidator::FixAnthropicTurns(msgs);
  // Empty message should be dropped; real content message should survive
  bool found_real = false;
  for (const auto& m : fixed) {
    for (const auto& b : m.content) {
      if (b.text == "real content") found_real = true;
    }
  }
  EXPECT_TRUE(found_real);
}

// Two consecutive user messages where the first is empty: only the second
// (non-empty) message should appear in results.
TEST(TurnValidatorEmptyContent, LeadingEmptyUserMsgDropped) {
  Message empty_user;
  empty_user.role = "user";

  std::vector<Message> msgs = {
      empty_user,
      make_msg("user", "actual text"),
  };

  auto fixed = TurnValidator::FixAnthropicTurns(msgs);
  ASSERT_EQ(fixed.size(), 1u);
  EXPECT_EQ(fixed[0].content.size(), 1u);
  EXPECT_EQ(fixed[0].content[0].text, "actual text");
}

// An empty content message between two non-consecutive messages should not
// break the alternation logic.
TEST(TurnValidatorEmptyContent, EmptyMsgBetweenDifferentRoles) {
  Message empty_assistant;
  empty_assistant.role = "assistant";

  std::vector<Message> msgs = {
      make_msg("user", "q"),
      empty_assistant,
      make_msg("assistant", "answer"),
  };

  // FixGoogleTurns merges consecutive assistant messages
  auto fixed = TurnValidator::FixGoogleTurns(msgs);
  // Empty assistant message should be dropped, leaving user + real assistant
  bool found_answer = false;
  for (const auto& m : fixed) {
    if (m.role == "assistant") {
      for (const auto& b : m.content) {
        if (b.text == "answer") found_answer = true;
      }
    }
  }
  EXPECT_TRUE(found_answer);
}

// ── combined: timestamp + empty content ──────────────────────────────────────

TEST(TurnValidatorCombined, EmptyMsgSkippedTimestampPreserved) {
  Message empty_user;
  empty_user.role = "user";
  empty_user.timestamp = "should-be-ignored";

  std::vector<Message> msgs = {
      make_msg("user", "first", "ts-first"),
      empty_user,
      make_msg("user", "second", "ts-second"),
  };

  auto fixed = TurnValidator::FixAnthropicTurns(msgs);
  ASSERT_EQ(fixed.size(), 1u);
  // Merged result: "first" + "second" combined; timestamp = ts-second
  EXPECT_EQ(fixed[0].timestamp, "ts-second");
  // Both text blocks should be present
  EXPECT_EQ(fixed[0].content.size(), 2u);
}

}  // namespace
}  // namespace ravbot
