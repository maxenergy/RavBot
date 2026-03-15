// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include "quantclaw/channels/telegram_typing_manager.hpp"

using namespace quantclaw::channels;

class TelegramTypingTest : public ::testing::Test {
protected:
    void SetUp() override {
        call_count_ = 0;
        last_chat_id_ = "";
    }

    std::atomic<int> call_count_{0};
    std::string last_chat_id_;
};

// 测试基本的启动和停止
TEST_F(TelegramTypingTest, StartStop) {
    TypingStateManager manager("test-chat-1", [this](const std::string& chat_id) {
        call_count_++;
        last_chat_id_ = chat_id;
    });

    EXPECT_FALSE(manager.IsRunning());
    EXPECT_EQ(manager.GetRefreshCount(), 0);

    manager.Start();
    EXPECT_TRUE(manager.IsRunning());
    EXPECT_EQ(manager.GetRefreshCount(), 1);  // 立即发送一次

    manager.Stop();
    EXPECT_FALSE(manager.IsRunning());
}

// 测试持续刷新
TEST_F(TelegramTypingTest, ContinuousRefresh) {
    TypingStateManager manager("test-chat-2", [this](const std::string& chat_id) {
        call_count_++;
        last_chat_id_ = chat_id;
    }, 1000);  // 1 秒刷新间隔

    manager.Start();
    EXPECT_EQ(manager.GetRefreshCount(), 1);

    // 等待 2.5 秒，应该刷新 2-3 次
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    int count = manager.GetRefreshCount();
    EXPECT_GE(count, 2);  // 至少 2 次（初始 + 2 次刷新）
    EXPECT_LE(count, 4);  // 最多 4 次

    manager.Stop();
    EXPECT_EQ(last_chat_id_, "test-chat-2");
}

// 测试快速停止
TEST_F(TelegramTypingTest, QuickStop) {
    TypingStateManager manager("test-chat-3", [this](const std::string& chat_id) {
        call_count_++;
    }, 4000);

    manager.Start();
    EXPECT_EQ(manager.GetRefreshCount(), 1);

    // 立即停止（不等待刷新）
    manager.Stop();

    int count = manager.GetRefreshCount();
    EXPECT_EQ(count, 1);  // 只有初始的一次
}

// 测试重复启动
TEST_F(TelegramTypingTest, DoubleStart) {
    TypingStateManager manager("test-chat-4", [this](const std::string& chat_id) {
        call_count_++;
    });

    manager.Start();
    int count1 = manager.GetRefreshCount();

    // 尝试再次启动（应该被忽略）
    manager.Start();
    int count2 = manager.GetRefreshCount();

    EXPECT_EQ(count1, count2);  // 计数不应该增加

    manager.Stop();
}

// 测试刷新间隔边界
TEST_F(TelegramTypingTest, RefreshIntervalBounds) {
    // 测试最小值限制
    TypingStateManager manager1("test-chat-5", [this](const std::string& chat_id) {
        call_count_++;
    }, 500);  // 小于 1000ms

    manager1.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    int count1 = manager1.GetRefreshCount();
    manager1.Stop();

    // 应该使用 1000ms 间隔，所以 1.5 秒内应该有 1-2 次刷新
    EXPECT_GE(count1, 1);
    EXPECT_LE(count1, 3);

    // 测试最大值限制
    TypingStateManager manager2("test-chat-6", [this](const std::string& chat_id) {
        call_count_++;
    }, 15000);  // 大于 10000ms

    manager2.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(11000));
    int count2 = manager2.GetRefreshCount();
    manager2.Stop();

    // 应该使用 10000ms 间隔，所以 11 秒内应该有 1-2 次刷新
    EXPECT_GE(count2, 1);
    EXPECT_LE(count2, 3);
}

// 测试 RAII 包装器
TEST_F(TelegramTypingTest, GuardRAII) {
    {
        TypingStateGuard guard("test-chat-7", [this](const std::string& chat_id) {
            call_count_++;
            last_chat_id_ = chat_id;
        }, 1000);

        EXPECT_TRUE(guard.GetManager()->IsRunning());
        EXPECT_EQ(guard.GetManager()->GetRefreshCount(), 1);

        // 等待一次刷新
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));

        EXPECT_GE(guard.GetManager()->GetRefreshCount(), 2);
    }
    // guard 析构，应该自动停止

    EXPECT_EQ(last_chat_id_, "test-chat-7");
    EXPECT_GE(call_count_, 2);
}

// 测试异常处理
TEST_F(TelegramTypingTest, ExceptionHandling) {
    int exception_count = 0;

    TypingStateManager manager("test-chat-8", [&exception_count](const std::string& chat_id) {
        exception_count++;
        if (exception_count == 2) {
            throw std::runtime_error("Test exception");
        }
    }, 1000);

    manager.Start();
    EXPECT_EQ(manager.GetRefreshCount(), 1);

    // 等待足够长的时间，让异常发生并继续
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    // 即使有异常，manager 应该继续运行
    EXPECT_TRUE(manager.IsRunning());
    EXPECT_GE(exception_count, 3);  // 应该继续尝试刷新

    manager.Stop();
}

// 测试长时间运行
TEST_F(TelegramTypingTest, LongRunning) {
    TypingStateManager manager("test-chat-9", [this](const std::string& chat_id) {
        call_count_++;
    }, 1000);

    manager.Start();

    // 模拟长时间任务（6 秒）
    std::this_thread::sleep_for(std::chrono::milliseconds(6000));

    int count = manager.GetRefreshCount();
    EXPECT_GE(count, 5);  // 至少 5 次（初始 + 5 次刷新）
    EXPECT_LE(count, 8);  // 最多 8 次

    manager.Stop();
}

// 测试多个并发 manager
TEST_F(TelegramTypingTest, MultipleConcurrent) {
    std::atomic<int> count1{0}, count2{0}, count3{0};

    TypingStateManager manager1("chat-1", [&count1](const std::string&) { count1++; }, 1000);
    TypingStateManager manager2("chat-2", [&count2](const std::string&) { count2++; }, 1000);
    TypingStateManager manager3("chat-3", [&count3](const std::string&) { count3++; }, 1000);

    manager1.Start();
    manager2.Start();
    manager3.Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    manager1.Stop();
    manager2.Stop();
    manager3.Stop();

    // 每个 manager 应该独立工作
    EXPECT_GE(count1.load(), 2);
    EXPECT_GE(count2.load(), 2);
    EXPECT_GE(count3.load(), 2);
}
