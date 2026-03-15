// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/gateway/gateway_server.hpp"
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace quantclaw::gateway {

GatewayServer::GatewayServer(int port, std::shared_ptr<spdlog::logger> logger)
    : port_(port), logger_(logger) {
    // 初始化 RouteManager
    // Requirements: 3.3.2
    route_manager_ = std::make_shared<RouteManager>(logger_);
    logger_->info("GatewayServer created on port {}", port_);
}

GatewayServer::~GatewayServer() {
    Stop();
}

void GatewayServer::Start() {
    if (running_) {
        logger_->warn("GatewayServer already running");
        return;
    }

    auto ws_server = std::make_unique<HttpAwareWebSocketServer>(port_, "0.0.0.0");
    ws_server->setHttpRedirectPort(http_redirect_port_);
    server_ = std::move(ws_server);

    server_->setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> state,
               ix::WebSocket& ws,
               const ix::WebSocketMessagePtr& msg) {
            on_connection(state, ws, msg);
        }
    );

    auto res = server_->listen();
    if (!res.first) {
        throw std::runtime_error("Failed to listen on port " + std::to_string(port_) +
                                 ": " + res.second);
    }

    server_->start();
    running_ = true;
    start_time_.store(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());

    // 启动 watchdog 线程
    start_watchdog();

    logger_->info("GatewayServer started on port {}", port_);
}

void GatewayServer::Stop() {
    if (!running_) return;

    running_ = false;

    // 停止 watchdog 线程
    stop_watchdog();

    if (server_) {
        server_->stop();
        server_.reset();
    }

    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.clear();
    ws_connections_.clear();

    logger_->info("GatewayServer stopped");
}

bool GatewayServer::IsRunning() const {
    return running_;
}

void GatewayServer::RegisterHandler(const std::string& method, RpcHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_[method] = std::move(handler);
    logger_->debug("Registered RPC handler: {}", method);
}

void GatewayServer::BroadcastEvent(const std::string& event, const nlohmann::json& payload) {
    RpcEvent evt;
    evt.event = event;
    evt.payload = payload;
    std::string msg = evt.ToJson().dump();

    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (auto& [id, ws] : ws_connections_) {
        if (ws && connections_.count(id)) {
            ws->send(msg);
        }
    }
}

void GatewayServer::SendEventTo(const std::string& connection_id, const RpcEvent& event) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    if (connections_.count(connection_id) == 0) {
        return;
    }
    auto it = ws_connections_.find(connection_id);
    if (it != ws_connections_.end() && it->second) {
        it->second->send(event.ToJson().dump());
    }
}

void GatewayServer::SendResponseTo(const std::string& connection_id,
                                    const std::string& rpc_request_id,
                                    bool ok,
                                    const nlohmann::json& payload_or_error) {
    RpcResponse resp;
    resp.id = rpc_request_id;
    resp.ok = ok;
    if (ok) {
        resp.payload = payload_or_error;
    } else {
        resp.error.message = payload_or_error.value("error", "unknown error");
        resp.error.code = payload_or_error.value("code", "INTERNAL_ERROR");
        resp.error.retryable = payload_or_error.value("retryable", false);
        resp.error.retry_after_ms = payload_or_error.value("retryAfterMs", 0);
    }

    std::lock_guard<std::mutex> lock(connections_mutex_);
    if (connections_.count(connection_id) == 0) return;
    auto it = ws_connections_.find(connection_id);
    if (it != ws_connections_.end() && it->second) {
        it->second->send(resp.ToJson().dump());
    }
}

size_t GatewayServer::GetConnectionCount() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_.size();
}

int64_t GatewayServer::GetUptimeSeconds() const {
    auto st = start_time_.load();
    if (!running_ || st == 0) {
        return 0;
    }
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return now - st;
}

nlohmann::json GatewayServer::BuildSnapshot() const {
    nlohmann::json snapshot;

    // Presence: list of connected, authenticated clients
    nlohmann::json presence = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (const auto& [id, client] : connections_) {
            if (!client.authenticated) continue;
            nlohmann::json entry;
            entry["connId"] = client.connection_id;
            entry["role"] = client.role;
            entry["scopes"] = client.scopes;
            if (!client.client_name.empty()) entry["clientName"] = client.client_name;
            if (!client.client_version.empty()) entry["version"] = client.client_version;
            if (!client.device_id.empty()) entry["deviceId"] = client.device_id;
            entry["ts"] = client.connected_at;
            presence.push_back(entry);
        }
    }

    snapshot["presence"] = presence;
    snapshot["health"] = nlohmann::json::object();
    snapshot["stateVersion"] = {{"presence", 1}, {"health", 0}};
    snapshot["uptimeMs"] = GetUptimeSeconds() * 1000;

    // Auth mode
    {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        snapshot["authMode"] = auth_mode_;
    }

    // Session defaults
    snapshot["sessionDefaults"] = {
        {"defaultAgentId", "main"},
        {"mainKey", "main"},
        {"mainSessionKey", "agent:main:main"}
    };

    return snapshot;
}

void GatewayServer::on_connection(std::shared_ptr<ix::ConnectionState> state,
                                   ix::WebSocket& ws,
                                   const ix::WebSocketMessagePtr& msg) {
    std::string conn_id = state->getId();

    switch (msg->type) {
        case ix::WebSocketMessageType::Open: {
            logger_->info("Client connected: {}", conn_id);

            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                ClientConnection client;
                client.connection_id = conn_id;
                client.connected_at = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                connections_[conn_id] = client;
                ws_connections_[conn_id] = &ws;
            }

            // Send challenge
            send_challenge(ws);
            break;
        }

        case ix::WebSocketMessageType::Close: {
            logger_->info("Client disconnected: {}", conn_id);
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_.erase(conn_id);
            ws_connections_.erase(conn_id);
            break;
        }

        case ix::WebSocketMessageType::Message: {
            handle_message(conn_id, ws, msg->str);
            break;
        }

        case ix::WebSocketMessageType::Error: {
            logger_->error("WebSocket error for {}: {}", conn_id, msg->errorInfo.reason);
            break;
        }

        default:
            break;
    }
}

void GatewayServer::handle_message(const std::string& conn_id,
                                    ix::WebSocket& ws,
                                    const std::string& data) {
    try {
        // 清理输入消息
        // Requirements: 3.3.1, 7.1, 13.1
        std::string sanitized_data = sanitizer_.SanitizeInput(data);

        auto j = nlohmann::json::parse(sanitized_data);
        auto type = ParseFrameType(j);

        switch (type) {
            case FrameType::kRequest: {
                auto request = RpcRequest::FromJson(j);

                // 为请求附加路由元数据
                // Requirements: 3.3.2, 6.1
                auto metadata = route_manager_->AttachMetadata(
                    "websocket",  // source_channel
                    conn_id,      // target_session
                    false         // is_external
                );

                // 记录活跃请求（用于中止）
                // Requirements: 3.3.3
                {
                    std::lock_guard<std::mutex> lock(active_requests_mutex_);
                    active_requests_[request.id] = conn_id;
                }

                // Special handling for connect.hello / connect (OpenClaw)
                bool is_openclaw = (request.method == methods::kOcConnect);
                if (request.method == methods::kConnectHello || is_openclaw) {
                    bool ok = handle_hello(conn_id, request.params, is_openclaw);
                    if (ok) {
                        HelloOkPayload hello_ok;
                        hello_ok.openclaw_format = is_openclaw;
                        hello_ok.conn_id = conn_id;
                        hello_ok.snapshot = BuildSnapshot();
                        auto resp = RpcResponse::success(request.id, hello_ok.ToJson());
                        ws.send(resp.ToJson().dump());

                        // 记录交付状态
                        // Requirements: 6.5
                        route_manager_->RecordDelivery(metadata.message_id, DeliveryStatus::kDelivered);
                    } else {
                        auto resp = RpcResponse::failure(request.id,
                            "Authentication failed", "AUTH_FAILED");
                        ws.send(resp.ToJson().dump());

                        // 记录交付失败
                        // Requirements: 6.5
                        route_manager_->RecordDelivery(metadata.message_id, DeliveryStatus::kFailed);
                    }

                    // 清理活跃请求
                    {
                        std::lock_guard<std::mutex> lock(active_requests_mutex_);
                        active_requests_.erase(request.id);
                    }
                    return;
                }

                handle_rpc_request(conn_id, ws, request);

                // 清理活跃请求
                {
                    std::lock_guard<std::mutex> lock(active_requests_mutex_);
                    active_requests_.erase(request.id);
                }
                break;
            }

            default:
                logger_->warn("Unexpected frame type from client: {}", sanitized_data);
                break;
        }
    } catch (const std::exception& e) {
        logger_->error("Failed to parse message from {}: {}", conn_id, e.what());
    }
}

void GatewayServer::handle_rpc_request(const std::string& conn_id,
                                        ix::WebSocket& ws,
                                        const RpcRequest& request) {
    logger_->info("RPC request from {}: {} (id={})", conn_id, request.method, request.id);

    RpcHandler handler;
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = handlers_.find(request.method);
        if (it == handlers_.end()) {
            auto resp = RpcResponse::failure(request.id,
                "Unknown method: " + request.method, "METHOD_NOT_FOUND");
            ws.send(resp.ToJson().dump());
            return;
        }
        handler = it->second;
    }

    ClientConnection* client = nullptr;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(conn_id);
        if (it != connections_.end()) {
            client = &it->second;
        }
    }

    if (!client) {
        auto resp = RpcResponse::failure(request.id,
            "Connection not found", "CONNECTION_NOT_FOUND");
        ws.send(resp.ToJson().dump());
        return;
    }

    // Enforce authentication: if auth mode is not "none", client must have sent hello
    std::string current_auth_mode;
    {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        current_auth_mode = auth_mode_;
    }
    if (current_auth_mode != "none" && !client->authenticated) {
        auto resp = RpcResponse::failure(request.id,
            "Not authenticated: send connect.hello first", "NOT_AUTHENTICATED");
        ws.send(resp.ToJson().dump());
        return;
    }

    // RBAC check
    if (rbac_checker_ && client->authenticated) {
        if (!rbac_checker_->IsAllowed(request.method, client->role, client->scopes)) {
            logger_->warn("RBAC denied: method={}, role={}, conn={}",
                          request.method, client->role, conn_id);
            auto resp = RpcResponse::failure(request.id,
                "Permission denied: insufficient scope for " + request.method,
                "PERMISSION_DENIED");
            ws.send(resp.ToJson().dump());
            return;
        }
    }

    // Rate limiting check
    if (rate_limiter_) {
        if (!rate_limiter_->Allow(conn_id)) {
            int retry = rate_limiter_->RetryAfter(conn_id);
            logger_->warn("Rate limited: conn={}, retry_after={}s", conn_id, retry);
            auto resp = RpcResponse::failure(request.id,
                "Rate limit exceeded. Retry after " + std::to_string(retry) + "s",
                "RATE_LIMITED", true, retry * 1000);
            ws.send(resp.ToJson().dump());
            return;
        }
    }

    // 记录待处理请求（用于超时管理）
    bool expect_final = (request.method == "agent.request" || request.method == "chat.send");
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(conn_id);
        if (it != connections_.end()) {
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            it->second.pending_requests[request.id] = PendingRequest(
                request.id, request.method, now_ms, expect_final
            );
        }
    }

    try {
        auto result = handler(request.params, *client);

        // 移除待处理请求
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(conn_id);
            if (it != connections_.end()) {
                it->second.pending_requests.erase(request.id);
            }
        }

        auto resp = RpcResponse::success(request.id, result);
        ws.send(resp.ToJson().dump());
    } catch (const std::exception& e) {
        // 移除待处理请求
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(conn_id);
            if (it != connections_.end()) {
                it->second.pending_requests.erase(request.id);
            }
        }

        logger_->error("RPC handler error for {}: {}", request.method, e.what());
        auto resp = RpcResponse::failure(request.id, e.what(), "HANDLER_ERROR");
        ws.send(resp.ToJson().dump());
    }
}

void GatewayServer::send_challenge(ix::WebSocket& ws) {
    // Generate nonce
    thread_local static std::random_device rd;
    thread_local static std::mt19937 gen(rd());
    thread_local static std::uniform_int_distribution<> dis(0, 15);
    static const char* hex = "0123456789abcdef";

    std::string nonce;
    nonce.reserve(32);
    for (int i = 0; i < 32; ++i) {
        nonce += hex[dis(gen)];
    }

    ConnectChallenge challenge;
    challenge.nonce = nonce;
    challenge.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    ws.send(challenge.ToJson().dump());
}

void GatewayServer::SetAuth(const std::string& mode, const std::string& token) {
    {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        auth_mode_ = mode;
        expected_token_ = token;
    }
    logger_->info("Gateway auth configured: mode={}", mode);
}

bool GatewayServer::handle_hello(const std::string& conn_id,
                                  const nlohmann::json& params,
                                  bool is_openclaw) {
    auto hello = ConnectHelloParams::FromJson(params);

    // If auth mode is "token", validate the token
    {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        if (auth_mode_ == "token" && !expected_token_.empty()) {
            // Check if this connection is locked out
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            auto lockout_it = auth_lockout_until_.find(conn_id);
            if (lockout_it != auth_lockout_until_.end() && now_ms < lockout_it->second) {
                int remaining_sec = (lockout_it->second - now_ms) / 1000;
                logger_->warn("Auth locked out for {}: {} seconds remaining", conn_id, remaining_sec);
                return false;
            }

            // Validate token
            if (hello.auth_token != expected_token_) {
                // Increment fail count
                auth_fail_counts_[conn_id]++;
                int fail_count = auth_fail_counts_[conn_id];

                logger_->warn("Auth failed for {}: bad token (attempt {}/{})",
                              conn_id, fail_count, auth_max_attempts_);

                // Lock out if max attempts exceeded
                if (fail_count >= auth_max_attempts_) {
                    int64_t lockout_until = now_ms + (auth_lockout_duration_sec_ * 1000);
                    auth_lockout_until_[conn_id] = lockout_until;
                    logger_->error("Auth locked out for {}: too many failed attempts ({})",
                                   conn_id, fail_count);
                }

                return false;
            }

            // Success - clear fail count
            auth_fail_counts_.erase(conn_id);
            auth_lockout_until_.erase(conn_id);
        }
    }
    // auth mode "none" → skip validation

    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(conn_id);
    if (it == connections_.end()) {
        return false;
    }

    it->second.role = hello.role;
    // Use client-provided scopes, or fill in defaults based on role
    if (hello.scopes.empty()) {
        it->second.scopes = DefaultScopes(RoleFromString(hello.role));
    } else {
        it->second.scopes = hello.scopes;
    }
    it->second.device_id = hello.device_id;
    it->second.client_name = hello.client_name;
    it->second.client_version = hello.client_version;
    it->second.authenticated = true;
    it->second.client_type = is_openclaw ? "openclaw" : "quantclaw";

    logger_->info("Client {} authenticated: role={}, client={}, type={}",
                  conn_id, hello.role, hello.client_name, it->second.client_type);
    return true;
}

// Watchdog: 启动定期检查线程
void GatewayServer::start_watchdog() {
    if (watchdog_running_) {
        return;
    }

    watchdog_running_ = true;
    watchdog_thread_ = std::thread([this]() {
        logger_->info("Watchdog thread started");

        while (watchdog_running_) {
            // 每 5 秒检查一次
            std::this_thread::sleep_for(std::chrono::seconds(5));

            if (!watchdog_running_) {
                break;
            }

            watchdog_tick();
        }

        logger_->info("Watchdog thread stopped");
    });
}

// Watchdog: 停止检查线程
void GatewayServer::stop_watchdog() {
    if (!watchdog_running_) {
        return;
    }

    watchdog_running_ = false;

    if (watchdog_thread_.joinable()) {
        watchdog_thread_.join();
    }
}

// Watchdog: 检查并清理超时请求
void GatewayServer::watchdog_tick() {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::vector<std::pair<std::string, std::string>> timed_out;  // (conn_id, request_id)

    {
        std::lock_guard<std::mutex> lock(connections_mutex_);

        for (auto& [conn_id, conn] : connections_) {
            std::vector<std::string> to_remove;

            for (auto& [req_id, pending] : conn.pending_requests) {
                // 如果是 expect_final 请求，跳过超时检查
                if (pending.expect_final) {
                    continue;
                }

                int64_t elapsed_ms = now_ms - pending.created_at;
                if (elapsed_ms > request_timeout_ms_) {
                    to_remove.push_back(req_id);
                    timed_out.push_back({conn_id, req_id});
                }
            }

            // 移除超时请求
            for (const auto& req_id : to_remove) {
                conn.pending_requests.erase(req_id);
            }
        }
    }

    // 发送超时响应
    for (const auto& [conn_id, req_id] : timed_out) {
        logger_->warn("Request timed out: conn_id={}, request_id={}, timeout={}ms",
                      conn_id, req_id, request_timeout_ms_);

        SendResponseTo(conn_id, req_id, false,
                       RpcError{"REQUEST_TIMEOUT",
                                "Request timed out after " + std::to_string(request_timeout_ms_) + "ms",
                                true,  // retryable
                                1000   // retry after 1s
                       }.ToJson());
    }

    if (!timed_out.empty()) {
        logger_->info("Watchdog cleaned up {} timed out requests", timed_out.size());
    }
}

// 中止正在执行的请求
// Requirements: 3.3.3
bool GatewayServer::AbortRequest(const std::string& connection_id,
                                  const std::string& request_id) {
    std::lock_guard<std::mutex> lock(active_requests_mutex_);

    auto it = active_requests_.find(request_id);
    if (it == active_requests_.end()) {
        logger_->warn("Request not found for abort: request_id={}", request_id);
        return false;
    }

    if (it->second != connection_id) {
        logger_->warn("Connection mismatch for abort: request_id={}, expected={}, got={}",
                      request_id, it->second, connection_id);
        return false;
    }

    // 移除活跃请求
    active_requests_.erase(it);

    // 发送中止事件
    RpcEvent abort_event;
    abort_event.event = "request.aborted";
    abort_event.payload = {
        {"requestId", request_id},
        {"reason", "User requested abort"}
    };
    SendEventTo(connection_id, abort_event);

    logger_->info("Request aborted: request_id={}, conn_id={}", request_id, connection_id);
    return true;
}

// 探测健康状态（用于降级检测）
HealthStatus GatewayServer::ProbeHealth() {
    std::lock_guard<std::mutex> lock(health_mutex_);

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // 更新最后检查时间
    last_health_check_ms_ = now_ms;

    // 检查服务器是否运行
    if (!running_) {
        health_status_.store(HealthStatus::kUnreachable);
        degraded_check_count_ = 0;
        return HealthStatus::kUnreachable;
    }

    // 检查连接数
    size_t conn_count = GetConnectionCount();

    // 检查是否有超时请求
    int timeout_count = 0;
    {
        std::lock_guard<std::mutex> conn_lock(connections_mutex_);
        for (const auto& [conn_id, conn] : connections_) {
            for (const auto& [req_id, pending] : conn.pending_requests) {
                if (!pending.expect_final) {
                    int64_t elapsed_ms = now_ms - pending.created_at;
                    if (elapsed_ms > request_timeout_ms_ * health_timeout_threshold_percent_ / 100) {
                        timeout_count++;
                    }
                }
            }
        }
    }

    // 降级检测逻辑
    // 1. 如果有大量超时请求（> health_max_timeout_count_），标记为降级
    // 2. 如果连续 health_degraded_check_count_ 次检测到降级条件，才真正降级
    bool should_degrade = (timeout_count > health_max_timeout_count_);

    if (should_degrade) {
        degraded_check_count_++;
        if (degraded_check_count_ >= health_degraded_check_count_) {
            health_status_.store(HealthStatus::kDegraded);
            logger_->warn("Gateway health degraded: timeout_count={}, conn_count={}",
                          timeout_count, conn_count);
            return HealthStatus::kDegraded;
        }
    } else {
        // 恢复健康
        if (degraded_check_count_ > 0) {
            degraded_check_count_--;
        }
        if (health_status_.load() == HealthStatus::kDegraded && degraded_check_count_ == 0) {
            health_status_.store(HealthStatus::kHealthy);
            logger_->info("Gateway health recovered");
        }
    }

    return health_status_.load();
}

void GatewayServer::ClearAuthLockout(const std::string& identifier) {
    std::lock_guard<std::mutex> lock(auth_mutex_);

    if (identifier.empty()) {
        // Clear all lockouts
        int cleared_count = auth_fail_counts_.size() + auth_lockout_until_.size();
        auth_fail_counts_.clear();
        auth_lockout_until_.clear();
        logger_->info("Cleared all auth lockout state ({} entries)", cleared_count);
    } else {
        // Clear specific identifier
        auth_fail_counts_.erase(identifier);
        auth_lockout_until_.erase(identifier);
        logger_->info("Cleared auth lockout state for {}", identifier);
    }
}

} // namespace quantclaw::gateway
