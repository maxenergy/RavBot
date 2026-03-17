// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ravbot/channels/channel_base.hpp"
#include <spdlog/spdlog.h>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>

namespace ravbot {

// Browser Extension channel adapter (WebSocket-based)
class ExtensionChannel : public Channel {
public:
    ExtensionChannel(const std::string& channel_id,
                     int port,
                     std::shared_ptr<spdlog::logger> logger);
    ~ExtensionChannel() override;

    void Start() override;
    void Stop() override;
    std::string GetChannelName() const override { return "extension"; }

    void SendMessage(const std::string& recipient, const std::string& message) override;
    void BroadcastMessage(const std::string& message);
    bool IsAllowed(const std::string& sender) const override;

private:
    std::string channel_id_;
    int port_;
    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;

    std::mutex queue_mutex_;
    std::queue<std::string> message_queue_;

    void ServerLoop();
    void HandleConnection(int client_socket);
};

} // namespace ravbot
