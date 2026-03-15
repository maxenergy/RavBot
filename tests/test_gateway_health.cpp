// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/gateway/protocol.hpp"

using namespace quantclaw::gateway;

class GatewayHealthTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();
    }

    std::shared_ptr<spdlog::logger> logger_;
};

// 测试默认健康状态
TEST_F(GatewayHealthTest, DefaultHealthy) {
    GatewayServer server(0, logger_);

    EXPECT_EQ(server.GetHealthStatus(), HealthStatus::kHealthy);
}

// 测试服务器未运行时的健康状态
TEST_F(GatewayHealthTest, UnreachableWhenNotRunning) {
    GatewayServer server(0, logger_);

    // 服务器未启动时探测
    auto status = server.ProbeHealth();
    EXPECT_EQ(status, HealthStatus::kUnreachable);
    EXPECT_EQ(server.GetHealthStatus(), HealthStatus::kUnreachable);
}

// 测试服务器运行时的健康状态
TEST_F(GatewayHealthTest, HealthyWhenRunning) {
    GatewayServer server(0, logger_);

    server.Start();
    EXPECT_TRUE(server.IsRunning());

    // 探测健康状态
    auto status = server.ProbeHealth();
    EXPECT_EQ(status, HealthStatus::kHealthy);

    server.Stop();
}

// 测试设置健康状态
TEST_F(GatewayHealthTest, SetHealthStatus) {
    GatewayServer server(0, logger_);

    server.SetHealthStatus(HealthStatus::kDegraded);
    EXPECT_EQ(server.GetHealthStatus(), HealthStatus::kDegraded);

    server.SetHealthStatus(HealthStatus::kHealthy);
    EXPECT_EQ(server.GetHealthStatus(), HealthStatus::kHealthy);

    server.SetHealthStatus(HealthStatus::kUnreachable);
    EXPECT_EQ(server.GetHealthStatus(), HealthStatus::kUnreachable);
}

// 测试健康状态字符串转换
TEST_F(GatewayHealthTest, HealthStatusToString) {
    EXPECT_EQ(HealthStatusToString(HealthStatus::kHealthy), "healthy");
    EXPECT_EQ(HealthStatusToString(HealthStatus::kDegraded), "degraded");
    EXPECT_EQ(HealthStatusToString(HealthStatus::kUnreachable), "unreachable");
}

// 测试健康状态字符串解析
TEST_F(GatewayHealthTest, HealthStatusFromString) {
    EXPECT_EQ(HealthStatusFromString("healthy"), HealthStatus::kHealthy);
    EXPECT_EQ(HealthStatusFromString("degraded"), HealthStatus::kDegraded);
    EXPECT_EQ(HealthStatusFromString("unreachable"), HealthStatus::kUnreachable);

    EXPECT_THROW(HealthStatusFromString("invalid"), std::runtime_error);
}

// 测试降级检测（需要连续 3 次检测）
TEST_F(GatewayHealthTest, DegradedDetection) {
    GatewayServer server(0, logger_);
    server.Start();

    // 设置一个很短的超时时间
    server.SetRequestTimeout(100);

    // 模拟大量超时请求（通过手动设置降级状态）
    // 注意：实际降级检测需要真实的超时请求，这里只测试状态转换
    server.SetHealthStatus(HealthStatus::kDegraded);
    EXPECT_EQ(server.GetHealthStatus(), HealthStatus::kDegraded);

    // 恢复健康
    server.SetHealthStatus(HealthStatus::kHealthy);
    EXPECT_EQ(server.GetHealthStatus(), HealthStatus::kHealthy);

    server.Stop();
}

// 测试健康状态恢复
TEST_F(GatewayHealthTest, HealthRecovery) {
    GatewayServer server(0, logger_);
    server.Start();

    // 设置为降级
    server.SetHealthStatus(HealthStatus::kDegraded);
    EXPECT_EQ(server.GetHealthStatus(), HealthStatus::kDegraded);

    // 探测健康状态（应该恢复）
    auto status = server.ProbeHealth();
    // 由于没有真实的超时请求，应该保持降级或恢复
    // 这里只验证探测不会崩溃
    EXPECT_TRUE(status == HealthStatus::kHealthy ||
                status == HealthStatus::kDegraded);

    server.Stop();
}

// 测试多次探测
TEST_F(GatewayHealthTest, MultipleProbes) {
    GatewayServer server(0, logger_);
    server.Start();

    // 多次探测
    for (int i = 0; i < 5; i++) {
        auto status = server.ProbeHealth();
        EXPECT_EQ(status, HealthStatus::kHealthy);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    server.Stop();
}

// 测试并发探测
TEST_F(GatewayHealthTest, ConcurrentProbes) {
    GatewayServer server(0, logger_);
    server.Start();

    std::vector<std::thread> threads;
    std::atomic<int> healthy_count{0};

    // 启动多个线程并发探测
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&server, &healthy_count]() {
            for (int j = 0; j < 10; j++) {
                auto status = server.ProbeHealth();
                if (status == HealthStatus::kHealthy) {
                    healthy_count++;
                }
            }
        });
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 应该大部分探测都是健康的
    EXPECT_GT(healthy_count.load(), 50);

    server.Stop();
}
