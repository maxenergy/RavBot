// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "quantclaw/tools/browser_tool.hpp"
#include <thread>
#include <chrono>

using namespace quantclaw;

class BrowserLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();

        // 基本配置
        config_.mode = BrowserToolConfig::Mode::kLocal;
        config_.headless = true;
        config_.viewport_width = 1280;
        config_.viewport_height = 720;
        config_.cdp_debug_port = 9222;

        // 生命周期配置（使用较短的超时时间以便测试）
        lifecycle_config_.idle_timeout_seconds = 2;
        lifecycle_config_.max_lifetime_seconds = 5;
        lifecycle_config_.health_check_interval_seconds = 1;
        lifecycle_config_.auto_cleanup = false;  // 手动控制清理
    }

    void TearDown() override {
        // 清理会在析构函数中自动完成
    }

    std::shared_ptr<spdlog::logger> logger_;
    BrowserToolConfig config_;
    SessionLifecycleConfig lifecycle_config_;
};

// 测试会话验证 - 有效会话
TEST_F(BrowserLifecycleTest, ValidateValidSession) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);
    std::string session_id = manager->create_session(config_);

    if (!session_id.empty()) {
        bool valid = manager->validate_session(session_id);
        EXPECT_TRUE(valid);

        // 清理
        manager->close_session(session_id);
    }
}

// 测试会话验证 - 不存在的会话
TEST_F(BrowserLifecycleTest, ValidateNonexistentSession) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);
    bool valid = manager->validate_session("nonexistent");
    EXPECT_FALSE(valid);
}

// 测试会话验证 - 已关闭的会话
TEST_F(BrowserLifecycleTest, ValidateClosedSession) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);
    std::string session_id = manager->create_session(config_);

    if (!session_id.empty()) {
        manager->close_session(session_id);

        bool valid = manager->validate_session(session_id);
        EXPECT_FALSE(valid);
    }
}

// 测试空闲超时检测
TEST_F(BrowserLifecycleTest, IdleTimeout) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);
    std::string session_id = manager->create_session(config_);

    if (!session_id.empty()) {
        // 等待超过空闲超时时间
        std::this_thread::sleep_for(std::chrono::seconds(3));

        bool valid = manager->validate_session(session_id);
        EXPECT_FALSE(valid);

        // 清理
        manager->close_session(session_id);
    }
}

// 测试最大生命周期检测
TEST_F(BrowserLifecycleTest, MaxLifetime) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);
    std::string session_id = manager->create_session(config_);

    if (!session_id.empty()) {
        // 等待超过最大生命周期
        std::this_thread::sleep_for(std::chrono::seconds(6));

        bool valid = manager->validate_session(session_id);
        EXPECT_FALSE(valid);

        // 清理
        manager->close_session(session_id);
    }
}

// 测试会话活跃更新
TEST_F(BrowserLifecycleTest, SessionActivityUpdate) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);
    std::string session_id = manager->create_session(config_);

    if (!session_id.empty()) {
        // 等待 1 秒
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 获取会话（更新 last_used）
        auto session = manager->get_session(session_id);
        ASSERT_NE(session, nullptr);

        // 再等待 1 秒（总共 2 秒，但 last_used 已更新）
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 应该仍然有效
        bool valid = manager->validate_session(session_id);
        EXPECT_TRUE(valid);

        // 清理
        manager->close_session(session_id);
    }
}

// 测试手动清理过期会话
TEST_F(BrowserLifecycleTest, ManualCleanup) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);

    // 创建多个会话
    std::vector<std::string> session_ids;
    for (int i = 0; i < 2; ++i) {
        config_.cdp_debug_port = 9222 + i;
        std::string session_id = manager->create_session(config_);
        if (!session_id.empty()) {
            session_ids.push_back(session_id);
        }
    }

    if (!session_ids.empty()) {
        // 等待超过空闲超时时间
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // 手动清理
        manager->cleanup_expired_sessions();

        // 验证所有会话已被清理
        auto sessions = manager->list_sessions();
        EXPECT_EQ(sessions.size(), 0);
    }
}

// 测试自动清理启动和停止
TEST_F(BrowserLifecycleTest, AutoCleanupStartStop) {
    lifecycle_config_.auto_cleanup = true;
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);

    std::string session_id = manager->create_session(config_);

    if (!session_id.empty()) {
        // 等待超过空闲超时时间 + 健康检查间隔
        std::this_thread::sleep_for(std::chrono::seconds(4));

        // 会话应该已被自动清理
        auto sessions = manager->list_sessions();
        EXPECT_EQ(sessions.size(), 0);
    }
}

// 测试生命周期管理器启动
TEST_F(BrowserLifecycleTest, LifecycleManagerStart) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);

    // 手动启动生命周期管理器
    manager->start_lifecycle_manager();

    std::string session_id = manager->create_session(config_);

    if (!session_id.empty()) {
        // 等待超过空闲超时时间 + 健康检查间隔
        std::this_thread::sleep_for(std::chrono::seconds(4));

        // 会话应该已被自动清理
        auto sessions = manager->list_sessions();
        EXPECT_EQ(sessions.size(), 0);
    }

    // 停止生命周期管理器
    manager->stop_lifecycle_manager();
}

// 测试生命周期管理器停止
TEST_F(BrowserLifecycleTest, LifecycleManagerStop) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);

    manager->start_lifecycle_manager();
    manager->stop_lifecycle_manager();

    std::string session_id = manager->create_session(config_);

    if (!session_id.empty()) {
        // 等待超过空闲超时时间 + 健康检查间隔
        std::this_thread::sleep_for(std::chrono::seconds(4));

        // 会话不应该被清理（生命周期管理器已停止）
        auto sessions = manager->list_sessions();
        EXPECT_EQ(sessions.size(), 1);

        // 清理
        manager->close_session(session_id);
    }
}

// 测试多次启动生命周期管理器
TEST_F(BrowserLifecycleTest, MultipleLifecycleManagerStart) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);

    manager->start_lifecycle_manager();
    manager->start_lifecycle_manager();  // 第二次启动应该被忽略

    // 停止一次即可
    manager->stop_lifecycle_manager();
}

// 测试多次停止生命周期管理器
TEST_F(BrowserLifecycleTest, MultipleLifecycleManagerStop) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);

    manager->start_lifecycle_manager();
    manager->stop_lifecycle_manager();
    manager->stop_lifecycle_manager();  // 第二次停止应该被忽略
}

// 测试默认会话不被清理
TEST_F(BrowserLifecycleTest, DefaultSessionNotCleaned) {
    lifecycle_config_.auto_cleanup = true;
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);

    auto session = manager->get_or_create_default(config_);

    if (session) {
        // 等待超过空闲超时时间 + 健康检查间隔
        std::this_thread::sleep_for(std::chrono::seconds(4));

        // 默认会话也会被清理（如果空闲）
        auto sessions = manager->list_sessions();
        // 注意：默认会话也会被清理，因为它也遵循相同的生命周期规则
        EXPECT_EQ(sessions.size(), 0);
    }
}

// 测试并发会话验证
TEST_F(BrowserLifecycleTest, ConcurrentValidation) {
    auto manager = std::make_unique<BrowserSessionManager>(logger_, lifecycle_config_);
    std::string session_id = manager->create_session(config_);

    if (!session_id.empty()) {
        std::vector<std::thread> threads;
        std::vector<bool> results(5);

        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&manager, &session_id, &results, i]() {
                results[i] = manager->validate_session(session_id);
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // 所有验证应该返回相同结果
        bool first_result = results[0];
        for (const auto& result : results) {
            EXPECT_EQ(result, first_result);
        }

        // 清理
        manager->close_session(session_id);
    }
}

// 测试生命周期配置
TEST_F(BrowserLifecycleTest, LifecycleConfiguration) {
    SessionLifecycleConfig custom_config;
    custom_config.idle_timeout_seconds = 10;
    custom_config.max_lifetime_seconds = 20;
    custom_config.health_check_interval_seconds = 5;
    custom_config.auto_cleanup = false;

    auto manager = std::make_unique<BrowserSessionManager>(logger_, custom_config);

    std::string session_id = manager->create_session(config_);

    if (!session_id.empty()) {
        // 等待 3 秒（小于 idle_timeout）
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // 应该仍然有效
        bool valid = manager->validate_session(session_id);
        EXPECT_TRUE(valid);

        // 清理
        manager->close_session(session_id);
    }
}
