// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ravbot/channels/channel_base.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <mutex>

namespace ravbot {

struct SlackConfig {
    std::string bot_token;  // xoxb-...
    std::string app_token;  // xapp-...
    std::string signing_secret;
    std::vector<std::string> allowed_channels;
    std::vector<std::string> allowed_users;
    bool use_socket_mode = true;  // Use Socket Mode or HTTP webhooks
    std::string webhook_url;
};

class SlackChannel : public Channel {
public:
    SlackChannel(const SlackConfig& config, std::shared_ptr<spdlog::logger> logger);
    ~SlackChannel() override;

    void Start() override;
    void Stop() override;
    void SendMessage(const std::string& channel_id, const std::string& message) override;
    bool IsAllowed(const std::string& sender_id) const override;
    std::string GetChannelName() const override { return "slack"; }

    // Slack-specific methods
    void SendThreadReply(const std::string& channel_id, const std::string& thread_ts, const std::string& message);
    void UploadFile(const std::string& channel_id, const std::string& file_path, const std::string& title = "");
    void AddReaction(const std::string& channel_id, const std::string& timestamp, const std::string& emoji);
    std::string GetUserInfo(const std::string& user_id);

private:
    void SocketModeLoop();
    void HandleEvent(const nlohmann::json& event);
    std::string MakeApiRequest(const std::string& endpoint, const std::string& method, const nlohmann::json& body = {});
    bool CheckSlackConnection();

    SlackConfig config_;
    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<bool> running_{false};
    std::thread socket_thread_;
    std::mutex send_mutex_;
};

} // namespace ravbot
