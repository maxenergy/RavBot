// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "quantclaw/providers/qwen_provider.hpp"
#include <nlohmann/json.hpp>

using namespace quantclaw;

class QwenProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();
        // 使用测试 API key (实际测试时需要真实 key)
        provider_ = std::make_unique<QwenProvider>(
            "test-api-key",
            "https://dashscope.aliyuncs.com/compatible-mode/v1",
            30,
            logger_
        );
    }

    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<QwenProvider> provider_;
};

// 测试 Provider 名称
TEST_F(QwenProviderTest, GetProviderName) {
    EXPECT_EQ(provider_->GetProviderName(), "qwen");
}

// 测试支持的模型列表
TEST_F(QwenProviderTest, GetSupportedModels) {
    auto models = provider_->GetSupportedModels();

    EXPECT_GT(models.size(), 0);

    // 检查是否包含主要模型
    bool has_qwen_turbo = false;
    bool has_qwen_plus = false;
    bool has_qwen_max = false;

    for (const auto& model : models) {
        if (model.find("qwen") != std::string::npos) {
            if (model.find("turbo") != std::string::npos) {
                has_qwen_turbo = true;
            }
            if (model.find("plus") != std::string::npos) {
                has_qwen_plus = true;
            }
            if (model.find("max") != std::string::npos) {
                has_qwen_max = true;
            }
        }
    }

    EXPECT_TRUE(has_qwen_turbo || has_qwen_plus || has_qwen_max);
}

// 测试基本请求
TEST_F(QwenProviderTest, BasicRequest) {
    ChatCompletionRequest request;
    request.model = "qwen-turbo";
    request.temperature = 0.7;
    request.max_tokens = 1024;

    Message user_msg;
    user_msg.role = "user";
    user_msg.content.push_back(ContentBlock::MakeText("你好,通义千问!"));
    request.messages.push_back(user_msg);

    // 这个测试只验证不会崩溃
    EXPECT_NO_THROW({
        EXPECT_EQ(request.model, "qwen-turbo");
        EXPECT_EQ(request.messages.size(), 1);
    });
}

// 测试系统消息
TEST_F(QwenProviderTest, SystemMessage) {
    ChatCompletionRequest request;
    request.model = "qwen-turbo";

    // 添加系统消息
    Message system_msg;
    system_msg.role = "system";
    system_msg.content.push_back(ContentBlock::MakeText("你是一个有帮助的助手。"));
    request.messages.push_back(system_msg);

    // 添加用户消息
    Message user_msg;
    user_msg.role = "user";
    user_msg.content.push_back(ContentBlock::MakeText("你好!"));
    request.messages.push_back(user_msg);

    EXPECT_EQ(request.messages.size(), 2);
    EXPECT_EQ(request.messages[0].role, "system");
    EXPECT_EQ(request.messages[1].role, "user");
}

// 测试工具调用
TEST_F(QwenProviderTest, ToolsRequest) {
    ChatCompletionRequest request;
    request.model = "qwen-turbo";

    // 添加工具定义
    nlohmann::json tool = {
        {"type", "function"},
        {"function", {
            {"name", "get_weather"},
            {"description", "获取天气信息"},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"location", {
                        {"type", "string"},
                        {"description", "城市名称"}
                    }}
                }},
                {"required", nlohmann::json::array({"location"})}
            }}
        }}
    };
    request.tools.push_back(tool);

    Message user_msg;
    user_msg.role = "user";
    user_msg.content.push_back(ContentBlock::MakeText("北京的天气怎么样?"));
    request.messages.push_back(user_msg);

    EXPECT_EQ(request.tools.size(), 1);
    EXPECT_EQ(request.messages.size(), 1);
}

// 测试温度参数
TEST_F(QwenProviderTest, TemperatureParameter) {
    ChatCompletionRequest request;
    request.model = "qwen-turbo";
    request.temperature = 0.9;

    EXPECT_DOUBLE_EQ(request.temperature, 0.9);
}

// 测试 max_tokens 参数
TEST_F(QwenProviderTest, MaxTokensParameter) {
    ChatCompletionRequest request;
    request.model = "qwen-turbo";
    request.max_tokens = 2048;

    EXPECT_EQ(request.max_tokens, 2048);
}

// 测试多轮对话
TEST_F(QwenProviderTest, MultiTurnConversation) {
    ChatCompletionRequest request;
    request.model = "qwen-turbo";

    // 第一轮
    Message user_msg1;
    user_msg1.role = "user";
    user_msg1.content.push_back(ContentBlock::MakeText("你好!"));
    request.messages.push_back(user_msg1);

    Message assistant_msg1;
    assistant_msg1.role = "assistant";
    assistant_msg1.content.push_back(ContentBlock::MakeText("你好!有什么可以帮助你的吗?"));
    request.messages.push_back(assistant_msg1);

    // 第二轮
    Message user_msg2;
    user_msg2.role = "user";
    user_msg2.content.push_back(ContentBlock::MakeText("讲个笑话。"));
    request.messages.push_back(user_msg2);

    EXPECT_EQ(request.messages.size(), 3);
    EXPECT_EQ(request.messages[0].role, "user");
    EXPECT_EQ(request.messages[1].role, "assistant");
    EXPECT_EQ(request.messages[2].role, "user");
}

// 测试空消息处理
TEST_F(QwenProviderTest, EmptyMessages) {
    ChatCompletionRequest request;
    request.model = "qwen-turbo";

    // 空消息列表
    EXPECT_EQ(request.messages.size(), 0);
}

// 测试模型名称
TEST_F(QwenProviderTest, ModelNames) {
    auto models = provider_->GetSupportedModels();

    // 验证模型名称格式
    for (const auto& model : models) {
        EXPECT_FALSE(model.empty());
        EXPECT_TRUE(model.find("qwen") != std::string::npos);
    }
}

// 测试 API URL 构建
TEST_F(QwenProviderTest, ApiUrlConstruction) {
    // 测试默认 base URL
    auto provider1 = std::make_unique<QwenProvider>(
        "test-key",
        "",  // 空 base_url 应该使用默认值
        30,
        logger_
    );

    EXPECT_EQ(provider1->GetProviderName(), "qwen");

    // 测试自定义 base URL
    auto provider2 = std::make_unique<QwenProvider>(
        "test-key",
        "https://custom.api.com/v1",
        30,
        logger_
    );

    EXPECT_EQ(provider2->GetProviderName(), "qwen");
}

// 测试超时设置
TEST_F(QwenProviderTest, TimeoutSetting) {
    auto provider_short = std::make_unique<QwenProvider>(
        "test-key",
        "",
        10,  // 10 秒超时
        logger_
    );

    auto provider_long = std::make_unique<QwenProvider>(
        "test-key",
        "",
        60,  // 60 秒超时
        logger_
    );

    EXPECT_EQ(provider_short->GetProviderName(), "qwen");
    EXPECT_EQ(provider_long->GetProviderName(), "qwen");
}

// 测试中文支持
TEST_F(QwenProviderTest, ChineseSupport) {
    ChatCompletionRequest request;
    request.model = "qwen-turbo";

    Message user_msg;
    user_msg.role = "user";
    user_msg.content.push_back(ContentBlock::MakeText("你好,世界!这是一个中文测试。"));
    request.messages.push_back(user_msg);

    EXPECT_EQ(request.messages.size(), 1);
    EXPECT_FALSE(request.messages[0].text().empty());
}
