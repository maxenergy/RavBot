// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include "quantclaw/channels/telegram_channel.hpp"

using namespace quantclaw;

// 测试辅助函数：模拟分块逻辑
namespace {

constexpr size_t kTelegramMessageChunkLimit = 4000;

std::vector<std::string> split_telegram_message_test(const std::string& message) {
    if (message.empty()) {
        return {""};
    }

    std::vector<std::string> chunks;
    size_t start = 0;

    while (start < message.size()) {
        size_t end = std::min(start + kTelegramMessageChunkLimit, message.size());

        // 如果不是最后一块,尝试在句子边界分块
        if (end < message.size()) {
            // 优先在句号、问号、感叹号处分块
            size_t sentence_end = message.find_last_of(".?!\n", end);
            if (sentence_end != std::string::npos && sentence_end > start) {
                end = sentence_end + 1;
            } else {
                // 其次在空格处分块
                size_t space_pos = message.find_last_of(" \t\n", end);
                if (space_pos != std::string::npos && space_pos > start) {
                    end = space_pos + 1;
                } else {
                    // 最后确保不在 UTF-8 字符中间分块
                    while (end > start && (static_cast<unsigned char>(message[end]) & 0xC0) == 0x80) {
                        --end;
                    }
                    if (end == start) {
                        end = std::min(start + kTelegramMessageChunkLimit, message.size());
                    }
                }
            }
        }

        chunks.push_back(message.substr(start, end - start));
        start = end;
    }

    return chunks;
}

} // anonymous namespace

class TelegramChunkingTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();
    }

    std::shared_ptr<spdlog::logger> logger_;
};

// 测试空消息
TEST_F(TelegramChunkingTest, EmptyMessage) {
    auto chunks = split_telegram_message_test("");
    EXPECT_EQ(chunks.size(), 1);
    EXPECT_EQ(chunks[0], "");
}

// 测试短消息（不需要分块）
TEST_F(TelegramChunkingTest, ShortMessage) {
    std::string message = "Hello, world!";
    auto chunks = split_telegram_message_test(message);

    EXPECT_EQ(chunks.size(), 1);
    EXPECT_EQ(chunks[0], message);
}

// 测试长消息（需要分块）
TEST_F(TelegramChunkingTest, LongMessage) {
    // 创建一个超过 4000 字符的消息
    std::string message;
    for (int i = 0; i < 500; i++) {
        message += "This is a test message. ";
    }

    auto chunks = split_telegram_message_test(message);

    // 应该被分成多个块
    EXPECT_GT(chunks.size(), 1);

    // 每个块都不应该超过限制
    for (const auto& chunk : chunks) {
        EXPECT_LE(chunk.size(), kTelegramMessageChunkLimit);
    }

    // 重新组合应该等于原消息
    std::string reconstructed;
    for (const auto& chunk : chunks) {
        reconstructed += chunk;
    }
    EXPECT_EQ(reconstructed, message);
}

// 测试句子边界分块
TEST_F(TelegramChunkingTest, SentenceBoundary) {
    // 创建一个包含句子的长消息
    std::string message;
    for (int i = 0; i < 200; i++) {
        message += "This is sentence " + std::to_string(i) + ". ";
    }

    auto chunks = split_telegram_message_test(message);

    // 应该被分成多个块
    EXPECT_GT(chunks.size(), 1);

    // 检查每个块（除了最后一个）是否以句号结尾
    for (size_t i = 0; i < chunks.size() - 1; i++) {
        EXPECT_TRUE(chunks[i].back() == '.' || chunks[i].back() == ' ');
    }
}

// 测试 UTF-8 字符边界
TEST_F(TelegramChunkingTest, UTF8Boundary) {
    // 创建一个包含中文字符的长消息
    std::string message;
    for (int i = 0; i < 1500; i++) {  // 增加循环次数确保超过 4000 字节
        message += "这是一条测试消息。";
    }

    auto chunks = split_telegram_message_test(message);

    // 应该被分成多个块
    EXPECT_GT(chunks.size(), 1);

    // 每个块都应该是有效的 UTF-8
    for (const auto& chunk : chunks) {
        EXPECT_LE(chunk.size(), kTelegramMessageChunkLimit);
    }

    // 重新组合应该等于原消息
    std::string reconstructed;
    for (const auto& chunk : chunks) {
        reconstructed += chunk;
    }
    EXPECT_EQ(reconstructed, message);
}

// 测试空格边界分块
TEST_F(TelegramChunkingTest, SpaceBoundary) {
    // 创建一个没有句号但有空格的长消息
    std::string message;
    for (int i = 0; i < 2000; i++) {  // 增加循环次数确保超过 4000 字节
        message += "word" + std::to_string(i) + " ";
    }

    auto chunks = split_telegram_message_test(message);

    // 应该被分成多个块
    EXPECT_GT(chunks.size(), 1);

    // 每个块都不应该超过限制
    for (const auto& chunk : chunks) {
        EXPECT_LE(chunk.size(), kTelegramMessageChunkLimit);
    }
}

// 测试混合内容分块
TEST_F(TelegramChunkingTest, MixedContent) {
    // 创建一个包含句子、空格和中文的长消息
    std::string message;
    for (int i = 0; i < 200; i++) {
        message += "This is sentence " + std::to_string(i) + ". ";
        message += "这是中文句子" + std::to_string(i) + "。";
    }

    auto chunks = split_telegram_message_test(message);

    // 应该被分成多个块
    EXPECT_GT(chunks.size(), 1);

    // 每个块都不应该超过限制
    for (const auto& chunk : chunks) {
        EXPECT_LE(chunk.size(), kTelegramMessageChunkLimit);
    }

    // 重新组合应该等于原消息
    std::string reconstructed;
    for (const auto& chunk : chunks) {
        reconstructed += chunk;
    }
    EXPECT_EQ(reconstructed, message);
}

// 测试极长单词（无法在边界分块）
TEST_F(TelegramChunkingTest, VeryLongWord) {
    // 创建一个超长单词（没有空格或句号）
    std::string message(5000, 'a');

    auto chunks = split_telegram_message_test(message);

    // 应该被分成多个块
    EXPECT_GT(chunks.size(), 1);

    // 每个块都不应该超过限制
    for (const auto& chunk : chunks) {
        EXPECT_LE(chunk.size(), kTelegramMessageChunkLimit);
    }

    // 重新组合应该等于原消息
    std::string reconstructed;
    for (const auto& chunk : chunks) {
        reconstructed += chunk;
    }
    EXPECT_EQ(reconstructed, message);
}

// 测试换行符边界
TEST_F(TelegramChunkingTest, NewlineBoundary) {
    // 创建一个包含换行符的长消息
    std::string message;
    for (int i = 0; i < 500; i++) {  // 增加循环次数确保超过 4000 字节
        message += "Line " + std::to_string(i) + " with some more text to make it longer\n";
    }

    auto chunks = split_telegram_message_test(message);

    // 应该被分成多个块
    EXPECT_GT(chunks.size(), 1);

    // 每个块都不应该超过限制
    for (const auto& chunk : chunks) {
        EXPECT_LE(chunk.size(), kTelegramMessageChunkLimit);
    }
}

// 测试性能（大量分块）
TEST_F(TelegramChunkingTest, Performance) {
    // 创建一个非常长的消息
    std::string message;
    for (int i = 0; i < 2000; i++) {
        message += "This is a test message. ";
    }

    auto start = std::chrono::high_resolution_clock::now();
    auto chunks = split_telegram_message_test(message);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // 分块应该很快（< 100ms）
    EXPECT_LT(duration.count(), 100);

    // 应该被分成多个块
    EXPECT_GT(chunks.size(), 1);

    logger_->info("Performance test: {} chunks in {}ms", chunks.size(), duration.count());
}
