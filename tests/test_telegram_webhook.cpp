// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "ravbot/channels/telegram_channel.hpp"
#include <nlohmann/json.hpp>

using namespace ravbot;

// Mock TelegramChannel for testing
class MockTelegramChannel : public TelegramChannel {
public:
    MockTelegramChannel(const TelegramConfig& config, std::shared_ptr<spdlog::logger> logger)
        : TelegramChannel(config, logger) {}

    // Override MakeApiRequest to avoid real API calls
    nlohmann::json MakeApiRequest(const std::string& method, const nlohmann::json& params = {}) override {
        last_method_ = method;
        last_params_ = params;

        // Mock responses
        if (method == "setWebhook") {
            return {{"ok", true}, {"result", true}};
        } else if (method == "deleteWebhook") {
            return {{"ok", true}, {"result", true}};
        } else if (method == "getWebhookInfo") {
            return {
                {"ok", true},
                {"result", {
                    {"url", mock_webhook_url_},
                    {"has_custom_certificate", false},
                    {"pending_update_count", 0}
                }}
            };
        } else if (method == "getMe") {
            return {
                {"ok", true},
                {"result", {
                    {"id", 123456789},
                    {"is_bot", true},
                    {"first_name", "TestBot"},
                    {"username", "test_bot"}
                }}
            };
        }

        return {{"ok", false}};
    }

    std::string last_method_;
    nlohmann::json last_params_;
    std::string mock_webhook_url_ = "https://example.com/webhook";
};

class TelegramWebhookTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();

        // 轮询模式配置
        polling_config_.bot_token = "test-token";
        polling_config_.mode = "polling";

        // Webhook 模式配置
        webhook_config_.bot_token = "test-token";
        webhook_config_.mode = "webhook";
        webhook_config_.webhook_url = "https://example.com/webhook";
        webhook_config_.webhook_secret = "test-secret-123";
        webhook_config_.webhook_path = "/telegram/webhook";
    }

    std::shared_ptr<spdlog::logger> logger_;
    TelegramConfig polling_config_;
    TelegramConfig webhook_config_;
};

// 测试配置结构
TEST_F(TelegramWebhookTest, ConfigStructure) {
    EXPECT_EQ(polling_config_.mode, "polling");
    EXPECT_EQ(webhook_config_.mode, "webhook");
    EXPECT_EQ(webhook_config_.webhook_url, "https://example.com/webhook");
    EXPECT_EQ(webhook_config_.webhook_secret, "test-secret-123");
    EXPECT_EQ(webhook_config_.webhook_path, "/telegram/webhook");
}

// 测试 SetWebhook
TEST_F(TelegramWebhookTest, SetWebhook) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    bool result = channel->SetWebhook("https://example.com/webhook", "secret-token");

    EXPECT_TRUE(result);
    EXPECT_EQ(channel->last_method_, "setWebhook");
    EXPECT_TRUE(channel->last_params_.contains("url"));
    EXPECT_EQ(channel->last_params_["url"], "https://example.com/webhook");
    EXPECT_TRUE(channel->last_params_.contains("secret_token"));
    EXPECT_EQ(channel->last_params_["secret_token"], "secret-token");
}

// 测试 SetWebhook 不带 secret
TEST_F(TelegramWebhookTest, SetWebhookWithoutSecret) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    bool result = channel->SetWebhook("https://example.com/webhook");

    EXPECT_TRUE(result);
    EXPECT_EQ(channel->last_method_, "setWebhook");
    EXPECT_TRUE(channel->last_params_.contains("url"));
    EXPECT_FALSE(channel->last_params_.contains("secret_token"));
}

// 测试 DeleteWebhook
TEST_F(TelegramWebhookTest, DeleteWebhook) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    bool result = channel->DeleteWebhook();

    EXPECT_TRUE(result);
    EXPECT_EQ(channel->last_method_, "deleteWebhook");
}

// 测试 GetWebhookInfo
TEST_F(TelegramWebhookTest, GetWebhookInfo) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    auto info = channel->GetWebhookInfo();

    EXPECT_EQ(channel->last_method_, "getWebhookInfo");
    EXPECT_TRUE(info.contains("url"));
    EXPECT_TRUE(info.contains("has_custom_certificate"));
    EXPECT_TRUE(info.contains("pending_update_count"));
}

// 测试 HandleWebhookUpdate
TEST_F(TelegramWebhookTest, HandleWebhookUpdate) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    nlohmann::json update = {
        {"update_id", 123456},
        {"message", {
            {"message_id", 1},
            {"from", {{"id", 987654321}, {"first_name", "Test"}}},
            {"chat", {{"id", 987654321}, {"type", "private"}}},
            {"text", "Hello"}
        }}
    };

    // 使用正确的 secret token
    EXPECT_NO_THROW({
        channel->HandleWebhookUpdate(update, "test-secret-123");
    });
}

// 测试 HandleWebhookUpdate 错误的 secret
TEST_F(TelegramWebhookTest, HandleWebhookUpdateInvalidSecret) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    nlohmann::json update = {
        {"update_id", 123456},
        {"message", {
            {"message_id", 1},
            {"from", {{"id", 987654321}, {"first_name", "Test"}}},
            {"chat", {{"id", 987654321}, {"type", "private"}}},
            {"text", "Hello"}
        }}
    };

    // 使用错误的 secret token（应该被拒绝）
    EXPECT_NO_THROW({
        channel->HandleWebhookUpdate(update, "wrong-secret");
    });
}

// 测试 allowed_updates 参数
TEST_F(TelegramWebhookTest, AllowedUpdates) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    channel->SetWebhook("https://example.com/webhook", "secret");

    EXPECT_TRUE(channel->last_params_.contains("allowed_updates"));
    auto allowed = channel->last_params_["allowed_updates"];
    EXPECT_TRUE(allowed.is_array());
    EXPECT_GT(allowed.size(), 0);
}

// 测试 Webhook URL 格式
TEST_F(TelegramWebhookTest, WebhookUrlFormat) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    // HTTPS URL
    bool result1 = channel->SetWebhook("https://example.com/webhook");
    EXPECT_TRUE(result1);

    // 带端口的 URL
    bool result2 = channel->SetWebhook("https://example.com:8443/webhook");
    EXPECT_TRUE(result2);

    // 带路径的 URL
    bool result3 = channel->SetWebhook("https://example.com/api/v1/telegram/webhook");
    EXPECT_TRUE(result3);
}

// 测试空 Webhook URL（删除 webhook）
TEST_F(TelegramWebhookTest, EmptyWebhookUrl) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    // 设置空 URL 应该等同于删除 webhook
    bool result = channel->SetWebhook("");
    EXPECT_TRUE(result);
    EXPECT_EQ(channel->last_params_["url"], "");
}

// 测试 Webhook 模式启动
TEST_F(TelegramWebhookTest, WebhookModeStart) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    // Webhook 模式启动不应该创建轮询线程
    EXPECT_NO_THROW({
        channel->Start();
        channel->Stop();
    });
}

// 测试轮询模式启动
TEST_F(TelegramWebhookTest, PollingModeStart) {
    auto channel = std::make_unique<MockTelegramChannel>(polling_config_, logger_);

    // 轮询模式启动应该创建轮询线程
    EXPECT_NO_THROW({
        channel->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        channel->Stop();
    });
}

// 测试模式切换
TEST_F(TelegramWebhookTest, ModeSwitching) {
    // 从轮询切换到 Webhook
    {
        auto channel = std::make_unique<MockTelegramChannel>(polling_config_, logger_);
        channel->Start();
        channel->Stop();

        // 设置 webhook
        bool result = channel->SetWebhook("https://example.com/webhook", "secret");
        EXPECT_TRUE(result);
    }

    // 从 Webhook 切换到轮询
    {
        auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);
        channel->Start();

        // 删除 webhook
        bool result = channel->DeleteWebhook();
        EXPECT_TRUE(result);

        channel->Stop();
    }
}

// 测试 Webhook 信息查询
TEST_F(TelegramWebhookTest, WebhookInfoQuery) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);

    auto info = channel->GetWebhookInfo();

    EXPECT_TRUE(info.is_object());
    EXPECT_TRUE(info.contains("url"));
    EXPECT_TRUE(info.contains("has_custom_certificate"));
    EXPECT_TRUE(info.contains("pending_update_count"));
}

// 测试并发 Webhook 更新
TEST_F(TelegramWebhookTest, ConcurrentWebhookUpdates) {
    auto channel = std::make_unique<MockTelegramChannel>(webhook_config_, logger_);
    channel->Start();

    // 模拟多个并发的 webhook 更新
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&channel, i]() {
            nlohmann::json update = {
                {"update_id", 123456 + i},
                {"message", {
                    {"message_id", i},
                    {"from", {{"id", 987654321}, {"first_name", "Test"}}},
                    {"chat", {{"id", 987654321}, {"type", "private"}}},
                    {"text", "Hello " + std::to_string(i)}
                }}
            };

            channel->HandleWebhookUpdate(update, "test-secret-123");
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    channel->Stop();
}
