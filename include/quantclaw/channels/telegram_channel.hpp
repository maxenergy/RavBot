// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "quantclaw/channels/channel_base.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_set>

namespace quantclaw {

struct TelegramConfig {
    std::string bot_token;
    std::string dm_policy = "open";        // open, closed, whitelist
    std::string group_policy = "open";     // open, closed, whitelist
    std::vector<std::string> allow_from;   // User IDs or "*"
    int history_limit = 10;
    int dm_history_limit = 15;
    std::string streaming = "partial";     // none, partial, full
    bool require_mention = false;          // Require @mention in groups
};

class TelegramChannel : public Channel {
public:
    TelegramChannel(const TelegramConfig& config, std::shared_ptr<spdlog::logger> logger);
    ~TelegramChannel() override;

    void Start() override;
    void Stop() override;
    void SendMessage(const std::string& chat_id, const std::string& message) override;
    bool IsAllowed(const std::string& sender_id) const override;
    std::string GetChannelName() const override { return "telegram"; }

    // Telegram-specific methods
    void SendReply(const std::string& chat_id, int message_id, const std::string& message);
    void SendPhoto(const std::string& chat_id, const std::string& photo_path, const std::string& caption = "");
    void SendDocument(const std::string& chat_id, const std::string& file_path, const std::string& caption = "");
    void EditMessage(const std::string& chat_id, int message_id, const std::string& new_text);
    void DeleteMessage(const std::string& chat_id, int message_id);
    nlohmann::json GetMe();
    nlohmann::json GetChat(const std::string& chat_id);

protected:
    virtual nlohmann::json MakeApiRequest(const std::string& method, const nlohmann::json& params = {});

private:
    void PollingLoop();
    void HandleUpdate(const nlohmann::json& update);
    void HandleMessage(const nlohmann::json& message);
    void HandleCallbackQuery(const nlohmann::json& callback_query);

    void SendTextChunks(const std::string& chat_id,
                        const std::string& message,
                        std::optional<int> reply_to_message_id = std::nullopt);
    std::string GetApiUrl(const std::string& method) const;
    bool CheckConnection();

    bool IsGroupChat(const nlohmann::json& chat) const;
    bool ShouldProcessMessage(const nlohmann::json& message) const;
    std::string ExtractChatId(const nlohmann::json& chat) const;
    std::string ExtractUserId(const nlohmann::json& user) const;

    void LoadLastUpdateId();
    void SaveLastUpdateId();

    TelegramConfig config_;
    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<bool> running_{false};
    std::thread polling_thread_;
    std::mutex send_mutex_;

    int64_t last_update_id_ = 0;
    std::string bot_username_;

    // Message deduplication
    std::unordered_set<std::string> processed_messages_;
    std::mutex processed_mutex_;
};

} // namespace quantclaw
