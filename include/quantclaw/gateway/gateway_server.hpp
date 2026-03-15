// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXHttp.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "quantclaw/gateway/protocol.hpp"
#include "quantclaw/gateway/message_sanitizer.hpp"
#include "quantclaw/gateway/route_manager.hpp"
#include "quantclaw/security/rbac.hpp"
#include "quantclaw/security/rate_limiter.hpp"
#include "quantclaw/common/noncopyable.hpp"

namespace quantclaw::gateway {

// WebSocket server that handles plain HTTP requests gracefully
// instead of logging "Missing Sec-WebSocket-Key" errors.
// When a browser or HTTP client hits the WS port, it returns a redirect
// to the Control UI HTTP port.
class HttpAwareWebSocketServer : public ix::WebSocketServer {
public:
    using ix::WebSocketServer::WebSocketServer;

    void setHttpRedirectPort(int port) { http_port_ = port; }

private:
    int http_port_ = 0;

    void handleConnection(std::unique_ptr<ix::Socket> socket,
                          std::shared_ptr<ix::ConnectionState> connectionState) override
    {
        // Parse the incoming HTTP request from the raw socket
        auto [ok, errMsg, request] = ix::Http::parseRequest(socket, getHandshakeTimeoutSecs());
        if (!ok) {
            // Malformed request — just close
            connectionState->setTerminated();
            return;
        }

        // Check if this is a proper WebSocket upgrade request
        bool has_ws_key = request->headers.find("sec-websocket-key") != request->headers.end();
        if (has_ws_key) {
            // Real WebSocket client — proceed with the normal upgrade path,
            // passing the already-parsed request so bytes aren't re-read.
            handleUpgrade(std::move(socket), connectionState, request);
            connectionState->setTerminated();
            return;
        }

        // Plain HTTP request (browser, curl, health-check, etc.)
        auto resp = std::make_shared<ix::HttpResponse>();
        if (http_port_ > 0) {
            resp->statusCode = 301;
            resp->description = "Moved Permanently";
            resp->headers["Location"] = "http://localhost:" + std::to_string(http_port_) + "/";
            resp->headers["Content-Type"] = "text/html";
            resp->body = "<html><body>Redirecting to <a href=\"http://localhost:"
                + std::to_string(http_port_) + "/\">dashboard</a></body></html>\n";
        } else {
            resp->statusCode = 426;
            resp->description = "Upgrade Required";
            resp->headers["Upgrade"] = "websocket";
            resp->headers["Content-Type"] = "text/plain";
            resp->body = "This is a WebSocket endpoint. Use a WebSocket client to connect.\n";
        }
        ix::Http::sendResponse(resp, socket);
        connectionState->setTerminated();
    }
};

using RpcHandler = std::function<nlohmann::json(const nlohmann::json& params, ClientConnection& client)>;

class GatewayServer : public quantclaw::Noncopyable {
public:
    GatewayServer(int port, std::shared_ptr<spdlog::logger> logger);
    ~GatewayServer();

    void Start();
    void Stop();
    bool IsRunning() const;

    void RegisterHandler(const std::string& method, RpcHandler handler);
    void BroadcastEvent(const std::string& event, const nlohmann::json& payload);
    void SendEventTo(const std::string& connection_id, const RpcEvent& event);
    void SendResponseTo(const std::string& connection_id,
                        const std::string& rpc_request_id,
                        bool ok, const nlohmann::json& payload_or_error);

    int GetPort() const { return port_; }
    size_t GetConnectionCount() const;
    int64_t GetUptimeSeconds() const;
    nlohmann::json BuildSnapshot() const;

    // Configure authentication
    void SetAuth(const std::string& mode, const std::string& token);
    std::string GetAuthMode() const {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        return auth_mode_;
    }

    // Configure auth lockout
    void SetAuthLockoutConfig(int max_attempts, int lockout_duration_sec) {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        auth_max_attempts_ = std::max(1, max_attempts);
        auth_lockout_duration_sec_ = std::max(1, lockout_duration_sec);
    }

    // Clear auth lockout state (for testing or manual unlock)
    void ClearAuthLockout(const std::string& identifier = "");

    // Set HTTP port for redirect when plain HTTP hits the WS port
    void SetHttpRedirectPort(int port) { http_redirect_port_ = port; }

    // Enable RBAC enforcement
    void SetRbac(std::shared_ptr<RBACChecker> checker) { rbac_checker_ = std::move(checker); }

    // Enable rate limiting
    void SetRateLimiter(std::shared_ptr<RateLimiter> limiter) { rate_limiter_ = std::move(limiter); }

    // 获取 MessageSanitizer（用于测试）
    // Requirements: 3.3.1
    MessageSanitizer* GetSanitizer() { return &sanitizer_; }

    // 获取 RouteManager（用于测试）
    // Requirements: 3.3.2
    RouteManager* GetRouteManager() { return route_manager_.get(); }

    // 中止正在执行的请求
    // Requirements: 3.3.3
    bool AbortRequest(const std::string& connection_id, const std::string& request_id);

    // 设置请求超时时间（毫秒）
    void SetRequestTimeout(int64_t timeout_ms) {
        request_timeout_ms_ = std::max(int64_t(1000), std::min(timeout_ms, int64_t(2147483647)));
    }

    // 获取请求超时时间
    int64_t GetRequestTimeout() const { return request_timeout_ms_; }

    // 获取健康状态
    HealthStatus GetHealthStatus() const { return health_status_.load(); }

    // 设置健康状态
    void SetHealthStatus(HealthStatus status) { health_status_.store(status); }

    // 探测健康状态（用于降级检测）
    HealthStatus ProbeHealth();

    // 设置健康监控配置
    void SetHealthConfig(int timeout_threshold_percent, int max_timeout_count, int degraded_check_count) {
        health_timeout_threshold_percent_ = std::max(1, std::min(timeout_threshold_percent, 100));
        health_max_timeout_count_ = std::max(1, max_timeout_count);
        health_degraded_check_count_ = std::max(1, degraded_check_count);
    }

private:
    void on_connection(std::shared_ptr<ix::ConnectionState> state,
                       ix::WebSocket& ws,
                       const ix::WebSocketMessagePtr& msg);
    void handle_message(const std::string& conn_id,
                        ix::WebSocket& ws,
                        const std::string& data);
    void handle_rpc_request(const std::string& conn_id,
                            ix::WebSocket& ws,
                            const RpcRequest& request);
    void send_challenge(ix::WebSocket& ws);
    bool handle_hello(const std::string& conn_id,
                      const nlohmann::json& params,
                      bool is_openclaw = false);

    // Watchdog: 定期检查并清理超时请求
    void start_watchdog();
    void stop_watchdog();
    void watchdog_tick();

    int port_;
    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<ix::WebSocketServer> server_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> start_time_{0};

    // Auth config (protected by auth_mutex_)
    mutable std::mutex auth_mutex_;
    std::string auth_mode_ = "token";
    std::string expected_token_;

    // Auth lockout tracking (protected by auth_mutex_)
    std::unordered_map<std::string, int> auth_fail_counts_;  // IP/conn_id -> fail count
    std::unordered_map<std::string, int64_t> auth_lockout_until_;  // IP/conn_id -> unlock time (ms)
    int auth_max_attempts_ = 5;  // Max failed attempts before lockout
    int auth_lockout_duration_sec_ = 300;  // Lockout duration (5 minutes)

    // HTTP redirect target (Control UI port)
    int http_redirect_port_ = 0;

    mutable std::mutex connections_mutex_;
    std::unordered_map<std::string, ClientConnection> connections_;
    // Non-owning WebSocket pointers. Lifetime managed by ixwebsocket:
    // valid from Open callback until Close callback. Always access under
    // connections_mutex_ and verify key exists in connections_ first.
    std::unordered_map<std::string, ix::WebSocket*> ws_connections_;

    std::mutex handlers_mutex_;
    std::unordered_map<std::string, RpcHandler> handlers_;

    // Security
    std::shared_ptr<RBACChecker> rbac_checker_;
    std::shared_ptr<RateLimiter> rate_limiter_;

    // 消息清理器
    // Requirements: 3.3.1
    MessageSanitizer sanitizer_;

    // 路由管理器
    // Requirements: 3.3.2
    std::shared_ptr<RouteManager> route_manager_;

    // 正在执行的请求（用于中止）
    // Requirements: 3.3.3
    std::mutex active_requests_mutex_;
    std::unordered_map<std::string, std::string> active_requests_;  // request_id -> connection_id

    // 请求超时配置和 watchdog
    int64_t request_timeout_ms_ = 30000;  // 默认 30 秒
    std::atomic<bool> watchdog_running_{false};
    std::thread watchdog_thread_;

    // 健康状态
    std::atomic<HealthStatus> health_status_{HealthStatus::kHealthy};
    mutable std::mutex health_mutex_;
    int64_t last_health_check_ms_ = 0;
    int degraded_check_count_ = 0;  // 降级检测计数器

    // 健康监控配置
    int health_timeout_threshold_percent_ = 80;  // 超时阈值百分比
    int health_max_timeout_count_ = 5;           // 最大超时数量
    int health_degraded_check_count_ = 3;        // 连续检测次数
};

} // namespace quantclaw::gateway
