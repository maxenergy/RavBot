// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "ravbot/channels/telegram_channel.hpp"

using namespace ravbot;

namespace {

class MockTelegramDeliveryChannel : public TelegramChannel {
 public:
  MockTelegramDeliveryChannel(const TelegramConfig& config,
                              std::shared_ptr<spdlog::logger> logger)
      : TelegramChannel(config, logger) {}

  nlohmann::json MakeApiRequest(
      const std::string& method,
      const nlohmann::json& params = {}) override {
    calls.push_back({method, params});
    if (method == "getMe") {
      return {{"ok", true},
              {"result",
               {{"id", 123456789},
                {"is_bot", true},
                {"first_name", "TestBot"},
                {"username", "test_bot"}}}};
    }
    if (method == "sendMessage") {
      if (send_fail_after >= 0 &&
          static_cast<int>(send_message_count) >= send_fail_after) {
        ++send_message_count;
        return {{"ok", false}, {"description", "mock send failure"}};
      }
      ++send_message_count;
      return {{"ok", true},
              {"result", {{"message_id", static_cast<int>(send_message_count)}}}};
    }
    return {{"ok", false}};
  }

  struct Call {
    std::string method;
    nlohmann::json params;
  };

  std::vector<Call> calls;
  size_t send_message_count = 0;
  int send_fail_after = -1;
};

class TelegramDeliveryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    logger_ = spdlog::default_logger();
    state_file_ = std::filesystem::temp_directory_path() /
                  "ravbot_test_telegram_delivery_state.json";
    std::filesystem::remove(state_file_);
    setenv("RAVBOT_TELEGRAM_STATE_FILE", state_file_.c_str(), 1);
    config_.bot_token = "test-token";
    config_.mode = "polling";
  }

  void TearDown() override {
    unsetenv("RAVBOT_TELEGRAM_STATE_FILE");
    std::filesystem::remove(state_file_);
  }

  std::shared_ptr<spdlog::logger> logger_;
  TelegramConfig config_;
  std::filesystem::path state_file_;
};

TEST_F(TelegramDeliveryTest, SendMessageCheckedReturnsTrueOnSuccessfulSend) {
  auto channel =
      std::make_unique<MockTelegramDeliveryChannel>(config_, logger_);

  EXPECT_TRUE(channel->SendMessageChecked("123456", "hello telegram"));
  ASSERT_EQ(channel->send_message_count, 1u);
  EXPECT_EQ(channel->calls.back().method, "sendMessage");
  EXPECT_EQ(channel->calls.back().params["chat_id"], "123456");
}

TEST_F(TelegramDeliveryTest,
       SendMessageCheckedReturnsFalseWhenChunkDeliveryFails) {
  auto channel =
      std::make_unique<MockTelegramDeliveryChannel>(config_, logger_);
  channel->send_fail_after = 1;

  std::string message;
  for (int i = 0; i < 500; ++i) {
    message += "This is a long Telegram response chunk. ";
  }

  EXPECT_FALSE(channel->SendMessageChecked("123456", message));
  EXPECT_EQ(channel->send_message_count, 2u);
}

}  // namespace
