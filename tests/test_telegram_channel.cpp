// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/null_sink.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "ravbot/channels/telegram_channel.hpp"

namespace {

std::shared_ptr<spdlog::logger> make_null_logger() {
    auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    return std::make_shared<spdlog::logger>("telegram-channel-test", sink);
}

class StubTelegramChannel : public ravbot::TelegramChannel {
public:
    explicit StubTelegramChannel(std::shared_ptr<spdlog::logger> logger)
        : TelegramChannel(ravbot::TelegramConfig{.bot_token = "test-token"}, logger) {}

    mutable std::vector<nlohmann::json> calls;

protected:
    nlohmann::json MakeApiRequest(const std::string& method,
                                  const nlohmann::json& params = {}) override {
        EXPECT_EQ(method, "sendMessage");
        calls.push_back(params);
        return {{"ok", true}};
    }
};

class StartupStubTelegramChannel : public ravbot::TelegramChannel {
public:
    explicit StartupStubTelegramChannel(std::shared_ptr<spdlog::logger> logger)
        : TelegramChannel(ravbot::TelegramConfig{.bot_token = "test-token"}, logger) {}

    std::atomic<int> get_me_calls{0};
    std::atomic<int> get_updates_calls{0};

protected:
    nlohmann::json MakeApiRequest(const std::string& method,
                                  const nlohmann::json& params = {}) override {
        if (method == "getMe") {
            if (++get_me_calls == 1) {
                return nlohmann::json::object();
            }
            return {{"ok", true}, {"result", {{"username", "cppclawbot"}}}};
        }
        if (method == "getUpdates") {
            ++get_updates_calls;
            return {{"ok", true}, {"result", nlohmann::json::array()}};
        }
        return {"ok", true};
    }
};

} // namespace

TEST(TelegramChannelTest, SendMessageSplitsLongMessagesIntoChunks) {
    StubTelegramChannel channel(make_null_logger());
    std::string message(9005, 'a');

    channel.SendMessage("chat-1", message);

    ASSERT_EQ(channel.calls.size(), 3u);
    EXPECT_EQ(channel.calls[0]["text"].get<std::string>().size(), 4000u);
    EXPECT_EQ(channel.calls[1]["text"].get<std::string>().size(), 4000u);
    EXPECT_EQ(channel.calls[2]["text"].get<std::string>().size(), 1005u);
    EXPECT_FALSE(channel.calls[0].contains("parse_mode"));
}

TEST(TelegramChannelTest, SendReplyUsesReplyToOnlyForFirstChunk) {
    StubTelegramChannel channel(make_null_logger());
    std::string message(4500, 'b');

    channel.SendReply("chat-2", 42, message);

    ASSERT_EQ(channel.calls.size(), 2u);
    EXPECT_EQ(channel.calls[0]["reply_to_message_id"], 42);
    EXPECT_FALSE(channel.calls[1].contains("reply_to_message_id"));
}

TEST(TelegramChannelTest, StartContinuesPollingWhenInitialGetMeFails) {
    StartupStubTelegramChannel channel(make_null_logger());

    channel.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    channel.Stop();

    EXPECT_GE(channel.get_me_calls.load(), 2);
    EXPECT_GE(channel.get_updates_calls.load(), 1);
}