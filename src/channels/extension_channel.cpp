// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/channels/extension_channel.hpp"
#include <chrono>
#include <thread>

namespace quantclaw {

ExtensionChannel::ExtensionChannel(const std::string& channel_id,
                                   int port,
                                   std::shared_ptr<spdlog::logger> logger)
    : channel_id_(channel_id),
      port_(port),
      logger_(logger) {}

ExtensionChannel::~ExtensionChannel() {
    Stop();
}

void ExtensionChannel::Start() {
    if (running_) {
        logger_->warn("Extension channel already running");
        return;
    }

    running_ = true;
    server_thread_ = std::thread(&ExtensionChannel::ServerLoop, this);
    logger_->info("Extension channel started on port {}: {}", port_, channel_id_);
}

void ExtensionChannel::Stop() {
    if (!running_) return;

    running_ = false;
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    logger_->info("Extension channel stopped: {}", channel_id_);
}

void ExtensionChannel::SendMessage(const std::string& recipient, const std::string& message) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    message_queue_.push(message);
    logger_->info("Extension message queued for {}: {}", recipient, message.substr(0, 50));
}

void ExtensionChannel::BroadcastMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    message_queue_.push(message);
    logger_->info("Extension broadcast queued: {}", message.substr(0, 50));
}

bool ExtensionChannel::IsAllowed(const std::string& sender) const {
    // TODO: Implement origin check for browser extensions
    return true;
}

void ExtensionChannel::ServerLoop() {
    logger_->info("Extension server loop started on port {}", port_);

    while (running_) {
        // TODO: Implement WebSocket server for browser extension communication
        // This would typically use a WebSocket library to handle connections
        // from browser extensions

        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Process queued messages
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!message_queue_.empty()) {
            std::string msg = message_queue_.front();
            message_queue_.pop();
            // TODO: Send to connected browser extensions
            logger_->debug("Processing queued message: {}", msg.substr(0, 50));
        }
    }

    logger_->info("Extension server loop stopped");
}

void ExtensionChannel::HandleConnection(int client_socket) {
    // TODO: Implement WebSocket handshake and message handling
    logger_->info("Extension connection handler called for socket {}", client_socket);
}

} // namespace quantclaw
