// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ravbot/channels/channel_base.hpp"
#include "ravbot/channels/deduplication_store.hpp"
#include "ravbot/channels/lane_processor.hpp"
#include "ravbot/channels/thread_binder.hpp"
#include "ravbot/channels/telegram_typing_manager.hpp"
#include "ravbot/gateway/message_sanitizer.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_set>

namespace ravbot {

struct TelegramConfig {
    std::string bot_token;
    std::string dm_policy = "open";        // open, closed, whitelist
    std::string group_policy = "open";     // open, closed, whitelist
    std::vector<std::string> allow_from;   // User IDs or "*"
    int history_limit = 10;
    int dm_history_limit = 15;
    std::string streaming = "partial";     // none, partial, full
    bool require_mention = false;          // Require @mention in groups

    // Webhook 配置
    std::string mode = "polling";          // polling, webhook
    std::string webhook_url;               // Webhook URL (for webhook mode)
    std::string webhook_secret;            // Secret token for webhook verification
    std::string webhook_path = "/telegram/webhook";  // Webhook endpoint path
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
    void SendChatAction(const std::string& chat_id, const std::string& action);
    nlohmann::json GetMe();
    nlohmann::json GetChat(const std::string& chat_id);

    // 媒体处理方法
    std::string DownloadFile(const std::string& file_id, const std::string& save_path = "");
    nlohmann::json GetFile(const std::string& file_id);

    // Webhook 方法
    bool SetWebhook(const std::string& url, const std::string& secret_token = "");
    bool DeleteWebhook();
    nlohmann::json GetWebhookInfo();
    void HandleWebhookUpdate(const nlohmann::json& update, const std::string& secret_token);

    // Set deduplication store
    // Requirements: 9.1, 9.2, 9.3
    void SetDeduplicationStore(std::shared_ptr<DeduplicationStore> store);

    // Set lane processor for sequential handling
    // Requirements: 9.4, 9.5, 9.6
    void SetLaneProcessor(std::shared_ptr<LaneProcessor> processor);

    // Set thread binder for topic support
    // Requirements: 10.1, 10.2, 10.3
    void SetThreadBinder(std::shared_ptr<ThreadBinder> binder);

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
    std::string GetFileUrl(const std::string& file_path) const;
    bool CheckConnection();

    // 媒体处理辅助方法
    nlohmann::json UploadFile(const std::string& method,
                              const std::string& chat_id,
                              const std::string& file_path,
                              const std::string& file_field,
                              const std::string& caption = "");
    std::string ReadFileContent(const std::string& file_path);
    std::string DownloadFileContent(const std::string& url);

    bool IsGroupChat(const nlohmann::json& chat) const;
    bool ShouldProcessMessage(const nlohmann::json& message) const;
    std::string ExtractChatId(const nlohmann::json& chat) const;
    std::string ExtractUserId(const nlohmann::json& user) const;

    void LoadLastUpdateId();
    void SaveLastUpdateId();

    // Check if update is duplicate
    // Requirements: 9.1, 9.3
    bool is_duplicate(int64_t update_id) const;

    // Record processed update
    // Requirements: 9.1, 9.2
    void record_update(int64_t update_id);

    // Get lane ID for message (chat_id for sequential processing)
    // Requirements: 9.4
    std::string get_lane_id(const nlohmann::json& message) const;

    // Extract thread ID from message
    // Requirements: 10.1
    std::optional<std::string> extract_thread_id(
        const nlohmann::json& message) const;

    TelegramConfig config_;
    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<bool> running_{false};
    std::thread polling_thread_;
    std::mutex send_mutex_;

    int64_t last_update_id_ = 0;
    std::string bot_username_;

    // Message deduplication (legacy, will be replaced by DeduplicationStore)
    std::unordered_set<std::string> processed_messages_;
    std::mutex processed_mutex_;

    // Enhanced components
    // Requirements: 9.1-9.8, 10.1-10.8
    std::shared_ptr<DeduplicationStore> dedup_store_;
    std::shared_ptr<LaneProcessor> lane_processor_;
    std::shared_ptr<ThreadBinder> thread_binder_;

    // Message sanitizer for cleaning output
    MessageSanitizer sanitizer_;
};

} // namespace ravbot
