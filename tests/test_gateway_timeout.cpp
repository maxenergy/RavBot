// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include "ravbot/gateway/gateway_server.hpp"
#include "ravbot/gateway/protocol.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

using namespace ravbot::gateway;

class GatewayTimeoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("test", null_sink);
        // 使用随机端口避免冲突
        int port = 19000 + (std::rand() % 1000);
        server_ = std::make_unique<GatewayServer>(port, logger_);
    }

    void TearDown() override {
        if (server_) {
            server_->Stop();
        }
    }

    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<GatewayServer> server_;
};

// 测试默认超时配置
TEST_F(GatewayTimeoutTest, DefaultTimeout) {
    EXPECT_EQ(server_->GetRequestTimeout(), 30000);  // 默认 30 秒
}

// 测试设置超时配置
TEST_F(GatewayTimeoutTest, SetTimeout) {
    server_->SetRequestTimeout(60000);
    EXPECT_EQ(server_->GetRequestTimeout(), 60000);

    // 测试最小值限制
    server_->SetRequestTimeout(500);
    EXPECT_EQ(server_->GetRequestTimeout(), 1000);  // 最小 1 秒

    // 测试最大值限制
    server_->SetRequestTimeout(3000000000LL);
    EXPECT_EQ(server_->GetRequestTimeout(), 2147483647);  // 最大值
}

// 测试 watchdog 启动和停止
TEST_F(GatewayTimeoutTest, WatchdogLifecycle) {
    server_->Start();
    EXPECT_TRUE(server_->IsRunning());

    // 等待 watchdog 启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server_->Stop();
    EXPECT_FALSE(server_->IsRunning());

    // 等待 watchdog 停止
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// 测试待处理请求记录
TEST_F(GatewayTimeoutTest, PendingRequestTracking) {
    // 创建模拟连接
    ClientConnection conn;
    conn.connection_id = "test-conn-1";
    conn.authenticated = true;

    // 添加待处理请求
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    PendingRequest req1("req-1", "test.method", now_ms, false);
    PendingRequest req2("req-2", "agent.request", now_ms, true);  // expect_final

    conn.pending_requests["req-1"] = req1;
    conn.pending_requests["req-2"] = req2;

    EXPECT_EQ(conn.pending_requests.size(), 2);
    EXPECT_FALSE(conn.pending_requests["req-1"].expect_final);
    EXPECT_TRUE(conn.pending_requests["req-2"].expect_final);
}

// 测试超时请求清理（模拟）
TEST_F(GatewayTimeoutTest, TimeoutCleanup) {
    server_->SetRequestTimeout(2000);  // 2 秒超时

    // 创建模拟连接
    ClientConnection conn;
    conn.connection_id = "test-conn-2";
    conn.authenticated = true;

    // 添加一个旧请求（已超时）
    auto old_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() - 3000;  // 3 秒前

    PendingRequest old_req("old-req", "test.method", old_time, false);
    conn.pending_requests["old-req"] = old_req;

    // 添加一个新请求（未超时）
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    PendingRequest new_req("new-req", "test.method", now_ms, false);
    conn.pending_requests["new-req"] = new_req;

    EXPECT_EQ(conn.pending_requests.size(), 2);

    // 模拟 watchdog 清理逻辑
    std::vector<std::string> to_remove;
    for (const auto& [req_id, pending] : conn.pending_requests) {
        if (pending.expect_final) {
            continue;
        }

        int64_t elapsed_ms = now_ms - pending.created_at;
        if (elapsed_ms > 2000) {
            to_remove.push_back(req_id);
        }
    }

    for (const auto& req_id : to_remove) {
        conn.pending_requests.erase(req_id);
    }

    // 验证只有旧请求被移除
    EXPECT_EQ(conn.pending_requests.size(), 1);
    EXPECT_TRUE(conn.pending_requests.count("new-req") > 0);
    EXPECT_TRUE(conn.pending_requests.count("old-req") == 0);
}

// 测试 expect_final 请求不会超时
TEST_F(GatewayTimeoutTest, ExpectFinalNoTimeout) {
    server_->SetRequestTimeout(1000);  // 1 秒超时

    ClientConnection conn;
    conn.connection_id = "test-conn-3";
    conn.authenticated = true;

    // 添加一个 expect_final 请求（很旧但不应超时）
    auto old_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() - 5000;  // 5 秒前

    PendingRequest final_req("final-req", "agent.request", old_time, true);
    conn.pending_requests["final-req"] = final_req;

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // 模拟 watchdog 清理逻辑
    std::vector<std::string> to_remove;
    for (const auto& [req_id, pending] : conn.pending_requests) {
        if (pending.expect_final) {
            continue;  // 跳过 expect_final 请求
        }

        int64_t elapsed_ms = now_ms - pending.created_at;
        if (elapsed_ms > 1000) {
            to_remove.push_back(req_id);
        }
    }

    for (const auto& req_id : to_remove) {
        conn.pending_requests.erase(req_id);
    }

    // 验证 expect_final 请求没有被移除
    EXPECT_EQ(conn.pending_requests.size(), 1);
    EXPECT_TRUE(conn.pending_requests.count("final-req") > 0);
}

// 测试多个连接的超时管理
TEST_F(GatewayTimeoutTest, MultipleConnections) {
    server_->SetRequestTimeout(2000);

    // 创建多个连接
    ClientConnection conn1, conn2;
    conn1.connection_id = "conn-1";
    conn2.connection_id = "conn-2";

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // conn1: 1 个超时请求
    conn1.pending_requests["req-1"] = PendingRequest("req-1", "test", now_ms - 3000, false);

    // conn2: 1 个正常请求
    conn2.pending_requests["req-2"] = PendingRequest("req-2", "test", now_ms, false);

    EXPECT_EQ(conn1.pending_requests.size(), 1);
    EXPECT_EQ(conn2.pending_requests.size(), 1);

    // 模拟清理 conn1
    std::vector<std::string> to_remove;
    for (const auto& [req_id, pending] : conn1.pending_requests) {
        int64_t elapsed_ms = now_ms - pending.created_at;
        if (elapsed_ms > 2000) {
            to_remove.push_back(req_id);
        }
    }

    for (const auto& req_id : to_remove) {
        conn1.pending_requests.erase(req_id);
    }

    EXPECT_EQ(conn1.pending_requests.size(), 0);  // conn1 的请求被清理
    EXPECT_EQ(conn2.pending_requests.size(), 1);  // conn2 的请求保留
}
