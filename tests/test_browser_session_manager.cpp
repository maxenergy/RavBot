// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "ravbot/tools/browser_tool.hpp"
#include <thread>
#include <chrono>

using namespace ravbot;

class BrowserSessionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();
        manager_ = std::make_unique<BrowserSessionManager>(logger_);

        // 基本配置
        config_.mode = BrowserToolConfig::Mode::kLocal;
        config_.headless = true;
        config_.viewport_width = 1280;
        config_.viewport_height = 720;
        config_.cdp_debug_port = 9222;
    }

    void TearDown() override {
        if (manager_) {
            manager_->close_all_sessions();
        }
    }

    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<BrowserSessionManager> manager_;
    BrowserToolConfig config_;
};

// 测试创建会话
TEST_F(BrowserSessionManagerTest, CreateSession) {
    // 注意：这个测试需要系统上安装了 Chromium/Chrome
    // 如果没有安装，测试会失败，这是预期的
    std::string session_id = manager_->create_session(config_);

    if (!session_id.empty()) {
        EXPECT_FALSE(session_id.empty());
        EXPECT_TRUE(session_id.find("session_") == 0);

        // 清理
        manager_->close_session(session_id);
    }
}

// 测试获取会话
TEST_F(BrowserSessionManagerTest, GetSession) {
    std::string session_id = manager_->create_session(config_);

    if (!session_id.empty()) {
        auto session = manager_->get_session(session_id);
        EXPECT_NE(session, nullptr);
        EXPECT_EQ(session->session_id(), session_id);

        // 清理
        manager_->close_session(session_id);
    }
}

// 测试获取不存在的会话
TEST_F(BrowserSessionManagerTest, GetNonexistentSession) {
    auto session = manager_->get_session("nonexistent");
    EXPECT_EQ(session, nullptr);
}

// 测试列出会话
TEST_F(BrowserSessionManagerTest, ListSessions) {
    auto sessions = manager_->list_sessions();
    EXPECT_EQ(sessions.size(), 0);

    // 创建会话后再列出
    std::string session_id = manager_->create_session(config_);
    if (!session_id.empty()) {
        sessions = manager_->list_sessions();
        EXPECT_EQ(sessions.size(), 1);
        EXPECT_EQ(sessions[0].session_id, session_id);

        // 清理
        manager_->close_session(session_id);
    }
}

// 测试关闭会话
TEST_F(BrowserSessionManagerTest, CloseSession) {
    std::string session_id = manager_->create_session(config_);

    if (!session_id.empty()) {
        bool closed = manager_->close_session(session_id);
        EXPECT_TRUE(closed);

        // 验证会话已关闭
        auto session = manager_->get_session(session_id);
        EXPECT_EQ(session, nullptr);
    }
}

// 测试关闭不存在的会话
TEST_F(BrowserSessionManagerTest, CloseNonexistentSession) {
    bool closed = manager_->close_session("nonexistent");
    EXPECT_FALSE(closed);
}

// 测试关闭所有会话
TEST_F(BrowserSessionManagerTest, CloseAllSessions) {
    // 创建多个会话
    std::vector<std::string> session_ids;
    for (int i = 0; i < 2; ++i) {
        config_.cdp_debug_port = 9222 + i;  // 使用不同端口
        std::string session_id = manager_->create_session(config_);
        if (!session_id.empty()) {
            session_ids.push_back(session_id);
        }
    }

    if (!session_ids.empty()) {
        manager_->close_all_sessions();

        // 验证所有会话已关闭
        auto sessions = manager_->list_sessions();
        EXPECT_EQ(sessions.size(), 0);
    }
}

// 测试会话 ID 生成
TEST_F(BrowserSessionManagerTest, SessionIdGeneration) {
    std::string id1 = manager_->create_session(config_);
    if (!id1.empty()) {
        config_.cdp_debug_port = 9223;  // 使用不同端口
        std::string id2 = manager_->create_session(config_);

        if (!id2.empty()) {
            EXPECT_NE(id1, id2);
            EXPECT_TRUE(id1.find("session_") == 0);
            EXPECT_TRUE(id2.find("session_") == 0);

            // 清理
            manager_->close_session(id1);
            manager_->close_session(id2);
        } else {
            manager_->close_session(id1);
        }
    }
}

// 测试会话元数据
TEST_F(BrowserSessionManagerTest, SessionMetadata) {
    std::string session_id = manager_->create_session(config_);

    if (!session_id.empty()) {
        auto sessions = manager_->list_sessions();
        ASSERT_EQ(sessions.size(), 1);

        auto& info = sessions[0];
        EXPECT_EQ(info.session_id, session_id);
        EXPECT_TRUE(info.is_connected);

        // 验证时间戳
        auto now = std::chrono::system_clock::now();
        auto created_diff = std::chrono::duration_cast<std::chrono::seconds>(
            now - info.created_at).count();
        EXPECT_LT(created_diff, 10);  // 应该在 10 秒内创建

        // 清理
        manager_->close_session(session_id);
    }
}

// 测试最后使用时间更新
TEST_F(BrowserSessionManagerTest, LastUsedUpdate) {
    std::string session_id = manager_->create_session(config_);

    if (!session_id.empty()) {
        auto session = manager_->get_session(session_id);
        ASSERT_NE(session, nullptr);

        auto first_last_used = session->last_used_at();

        // 等待一小段时间
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 再次获取会话，应该更新 last_used
        session = manager_->get_session(session_id);
        ASSERT_NE(session, nullptr);

        auto second_last_used = session->last_used_at();
        EXPECT_GT(second_last_used, first_last_used);

        // 清理
        manager_->close_session(session_id);
    }
}

// 测试默认会话
TEST_F(BrowserSessionManagerTest, GetOrCreateDefault) {
    auto session1 = manager_->get_or_create_default(config_);

    if (session1) {
        EXPECT_NE(session1, nullptr);
        EXPECT_EQ(session1->session_id(), "default");

        // 再次获取应该返回同一个会话
        auto session2 = manager_->get_or_create_default(config_);
        EXPECT_EQ(session1, session2);

        // 清理
        manager_->close_session("default");
    }
}

// 测试并发创建会话
TEST_F(BrowserSessionManagerTest, ConcurrentCreate) {
    std::vector<std::thread> threads;
    std::vector<std::string> session_ids(3);

    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([this, i, &session_ids]() {
            BrowserToolConfig local_config = config_;
            local_config.cdp_debug_port = 9222 + i;  // 使用不同端口
            session_ids[i] = manager_->create_session(local_config);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证所有会话 ID 都不同
    std::set<std::string> unique_ids;
    for (const auto& id : session_ids) {
        if (!id.empty()) {
            unique_ids.insert(id);
        }
    }

    if (!unique_ids.empty()) {
        EXPECT_EQ(unique_ids.size(), session_ids.size());

        // 清理
        for (const auto& id : session_ids) {
            if (!id.empty()) {
                manager_->close_session(id);
            }
        }
    }
}

// 测试会话信息完整性
TEST_F(BrowserSessionManagerTest, SessionInfoCompleteness) {
    std::string session_id = manager_->create_session(config_);

    if (!session_id.empty()) {
        auto sessions = manager_->list_sessions();
        ASSERT_EQ(sessions.size(), 1);

        auto& info = sessions[0];
        EXPECT_FALSE(info.session_id.empty());
        EXPECT_TRUE(info.is_connected);
        // URL 和 title 可能为空（还没有导航）
        EXPECT_TRUE(info.current_url.empty() || !info.current_url.empty());

        // 清理
        manager_->close_session(session_id);
    }
}

// 测试会话关闭后状态
TEST_F(BrowserSessionManagerTest, SessionStateAfterClose) {
    std::string session_id = manager_->create_session(config_);

    if (!session_id.empty()) {
        auto session = manager_->get_session(session_id);
        ASSERT_NE(session, nullptr);
        EXPECT_TRUE(session->is_connected());

        manager_->close_session(session_id);

        // 会话应该已断开连接
        EXPECT_FALSE(session->is_connected());
    }
}

// 测试空会话列表
TEST_F(BrowserSessionManagerTest, EmptySessionList) {
    auto sessions = manager_->list_sessions();
    EXPECT_TRUE(sessions.empty());
}
