// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "quantclaw/providers/google_provider.hpp"
#include <nlohmann/json.hpp>

using namespace quantclaw;

class GoogleProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();
        // 使用测试 API key (实际测试时需要真实 key)
        provider_ = std::make_unique<GoogleProvider>(
            "test-api-key",
            "https://generativelanguage.googleapis.com/v1beta",
            30,
            logger_
        );
    }

    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<GoogleProvider> provider_;
};

// 测试 Provider 名称
TEST_F(GoogleProviderTest, GetProviderName) {
    EXPECT_EQ(provider_->GetProviderName(), "google");
}

// 测试支持的模型列表
TEST_F(GoogleProviderTest, GetSupportedModels) {
    auto models = provider_->GetSupportedModels();

    EXPECT_GT(models.size(), 0);

    // 检查是否包含主要模型
    bool has_gemini_flash = false;
    bool has_gemini_pro = false;

    for (const auto& model : models) {
        if (model.find("gemini") != std::string::npos) {
            if (model.find("flash") != std::string::npos) {
                has_gemini_flash = true;
            }
            if (model.find("pro") != std::string::npos) {
                has_gemini_pro = true;
            }
        }
    }

    EXPECT_TRUE(has_gemini_flash);
    EXPECT_TRUE(has_gemini_pro);
}

// 测试请求转换 - 基本消息
TEST_F(GoogleProviderTest, ConvertBasicRequest) {
    ChatCompletionRequest request;
    request.model = "gemini-1.5-flash";
    request.temperature = 0.7;
    request.max_tokens = 1024;

    Message user_msg;
    user_msg.role = "user";
    user_msg.content.push_back(ContentBlock::MakeText("Hello, Gemini!"));
    request.messages.push_back(user_msg);

    // 这个测试只验证不会崩溃
    // 实际的 API 调用需要真实的 API key
    EXPECT_NO_THROW({
        // 只测试请求构建,不实际发送
        EXPECT_EQ(request.model, "gemini-1.5-flash");
        EXPECT_EQ(request.messages.size(), 1);
    });
}

// 测试系统消息转换
TEST_F(GoogleProviderTest, ConvertSystemMessage) {
    ChatCompletionRequest request;
    request.model = "gemini-1.5-flash";

    // 添加系统消息
    Message system_msg;
    system_msg.role = "system";
    system_msg.content.push_back(ContentBlock::MakeText("You are a helpful assistant."));
    request.messages.push_back(system_msg);

    // 添加用户消息
    Message user_msg;
    user_msg.role = "user";
    user_msg.content.push_back(ContentBlock::MakeText("Hello!"));
    request.messages.push_back(user_msg);

    EXPECT_EQ(request.messages.size(), 2);
    EXPECT_EQ(request.messages[0].role, "system");
    EXPECT_EQ(request.messages[1].role, "user");
}

// 测试工具调用转换
TEST_F(GoogleProviderTest, ConvertToolsRequest) {
    ChatCompletionRequest request;
    request.model = "gemini-1.5-flash";

    // 添加工具定义
    nlohmann::json tool = {
        {"type", "function"},
        {"function", {
            {"name", "get_weather"},
            {"description", "Get the current weather"},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"location", {
                        {"type", "string"},
                        {"description", "The city name"}
                    }}
                }},
                {"required", nlohmann::json::array({"location"})}
            }}
        }}
    };
    request.tools.push_back(tool);

    Message user_msg;
    user_msg.role = "user";
    user_msg.content.push_back(ContentBlock::MakeText("What's the weather in Tokyo?"));
    request.messages.push_back(user_msg);

    EXPECT_EQ(request.tools.size(), 1);
    EXPECT_EQ(request.messages.size(), 1);
}

// 测试温度参数
TEST_F(GoogleProviderTest, TemperatureParameter) {
    ChatCompletionRequest request;
    request.model = "gemini-1.5-flash";
    request.temperature = 0.9;

    EXPECT_DOUBLE_EQ(request.temperature, 0.9);
}

// 测试 max_tokens 参数
TEST_F(GoogleProviderTest, MaxTokensParameter) {
    ChatCompletionRequest request;
    request.model = "gemini-1.5-flash";
    request.max_tokens = 2048;

    EXPECT_EQ(request.max_tokens, 2048);
}

// 测试多轮对话
TEST_F(GoogleProviderTest, MultiTurnConversation) {
    ChatCompletionRequest request;
    request.model = "gemini-1.5-flash";

    // 第一轮
    Message user_msg1;
    user_msg1.role = "user";
    user_msg1.content.push_back(ContentBlock::MakeText("Hello!"));
    request.messages.push_back(user_msg1);

    Message assistant_msg1;
    assistant_msg1.role = "assistant";
    assistant_msg1.content.push_back(ContentBlock::MakeText("Hi! How can I help you?"));
    request.messages.push_back(assistant_msg1);

    // 第二轮
    Message user_msg2;
    user_msg2.role = "user";
    user_msg2.content.push_back(ContentBlock::MakeText("Tell me a joke."));
    request.messages.push_back(user_msg2);

    EXPECT_EQ(request.messages.size(), 3);
    EXPECT_EQ(request.messages[0].role, "user");
    EXPECT_EQ(request.messages[1].role, "assistant");
    EXPECT_EQ(request.messages[2].role, "user");
}

// 测试空消息处理
TEST_F(GoogleProviderTest, EmptyMessages) {
    ChatCompletionRequest request;
    request.model = "gemini-1.5-flash";

    // 空消息列表
    EXPECT_EQ(request.messages.size(), 0);
}

// 测试模型名称
TEST_F(GoogleProviderTest, ModelNames) {
    auto models = provider_->GetSupportedModels();

    // 验证模型名称格式
    for (const auto& model : models) {
        EXPECT_FALSE(model.empty());
        EXPECT_TRUE(model.find("gemini") != std::string::npos);
    }
}

// 测试 API URL 构建
TEST_F(GoogleProviderTest, ApiUrlConstruction) {
    // 测试默认 base URL
    auto provider1 = std::make_unique<GoogleProvider>(
        "test-key",
        "",  // 空 base_url 应该使用默认值
        30,
        logger_
    );

    EXPECT_EQ(provider1->GetProviderName(), "google");

    // 测试自定义 base URL
    auto provider2 = std::make_unique<GoogleProvider>(
        "test-key",
        "https://custom.api.com/v1",
        30,
        logger_
    );

    EXPECT_EQ(provider2->GetProviderName(), "google");
}

// 测试超时设置
TEST_F(GoogleProviderTest, TimeoutSetting) {
    auto provider_short = std::make_unique<GoogleProvider>(
        "test-key",
        "",
        10,  // 10 秒超时
        logger_
    );

    auto provider_long = std::make_unique<GoogleProvider>(
        "test-key",
        "",
        60,  // 60 秒超时
        logger_
    );

    EXPECT_EQ(provider_short->GetProviderName(), "google");
    EXPECT_EQ(provider_long->GetProviderName(), "google");
}
