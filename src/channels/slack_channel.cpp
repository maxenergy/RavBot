// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/channels/slack_channel.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <algorithm>
#include <fstream>

namespace quantclaw {

SlackChannel::SlackChannel(const SlackConfig& config, std::shared_ptr<spdlog::logger> logger)
    : config_(config), logger_(logger) {}

SlackChannel::~SlackChannel() {
    Stop();
}

bool SlackChannel::CheckSlackConnection() {
    try {
        // Test API connection with auth.test
        std::string response_str = MakeApiRequest("auth.test", "POST");
        auto response = nlohmann::json::parse(response_str);

        if (response.contains("ok") && response["ok"] == true) {
            logger_->info("Slack connection successful. Bot user: {}",
                response.value("user", "unknown"));
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        logger_->error("Slack connection failed: {}", e.what());
        return false;
    }
}

void SlackChannel::Start() {
    if (running_) {
        logger_->warn("Slack channel already running");
        return;
    }

    if (!CheckSlackConnection()) {
        throw std::runtime_error("Failed to connect to Slack API");
    }

    running_ = true;

    if (config_.use_socket_mode) {
        socket_thread_ = std::thread(&SlackChannel::SocketModeLoop, this);
        logger_->info("Slack channel started in Socket Mode");
    } else {
        logger_->info("Slack channel started in Webhook Mode");
    }
}

void SlackChannel::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (socket_thread_.joinable()) {
        socket_thread_.join();
    }
    logger_->info("Slack channel stopped");
}

std::string SlackChannel::MakeApiRequest(const std::string& endpoint,
                                        const std::string& method,
                                        const nlohmann::json& body) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string url = "https://slack.com/api/" + endpoint;
    std::string response_data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + config_.bot_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!body.empty()) {
            std::string body_str = body.dump();
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        }
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                         auto* str = static_cast<std::string*>(userdata);
                         str->append(ptr, size * nmemb);
                         return size * nmemb;
                     });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        throw std::runtime_error("Slack API returned HTTP " + std::to_string(http_code));
    }

    return response_data;
}

void SlackChannel::SendMessage(const std::string& channel_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    try {
        nlohmann::json request = {
            {"channel", channel_id},
            {"text", message}
        };

        std::string response_str = MakeApiRequest("chat.postMessage", "POST", request);
        auto response = nlohmann::json::parse(response_str);

        if (!response.contains("ok") || response["ok"] != true) {
            std::string error = response.value("error", "unknown");
            throw std::runtime_error("Slack API error: " + error);
        }

        logger_->debug("Sent Slack message to {}: {}", channel_id, message.substr(0, 50));

    } catch (const std::exception& e) {
        logger_->error("Failed to send Slack message: {}", e.what());
        throw;
    }
}

void SlackChannel::SendThreadReply(const std::string& channel_id,
                                  const std::string& thread_ts,
                                  const std::string& message) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    try {
        nlohmann::json request = {
            {"channel", channel_id},
            {"text", message},
            {"thread_ts", thread_ts}
        };

        std::string response_str = MakeApiRequest("chat.postMessage", "POST", request);
        auto response = nlohmann::json::parse(response_str);

        if (!response.contains("ok") || response["ok"] != true) {
            std::string error = response.value("error", "unknown");
            throw std::runtime_error("Slack API error: " + error);
        }

        logger_->debug("Sent Slack thread reply to {}", channel_id);

    } catch (const std::exception& e) {
        logger_->error("Failed to send Slack thread reply: {}", e.what());
        throw;
    }
}

void SlackChannel::UploadFile(const std::string& channel_id,
                             const std::string& file_path,
                             const std::string& title) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    try {
        // Read file content
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open file: " + file_path);
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

        nlohmann::json request = {
            {"channels", channel_id},
            {"content", content},
            {"filename", std::filesystem::path(file_path).filename().string()}
        };

        if (!title.empty()) {
            request["title"] = title;
        }

        std::string response_str = MakeApiRequest("files.upload", "POST", request);
        auto response = nlohmann::json::parse(response_str);

        if (!response.contains("ok") || response["ok"] != true) {
            std::string error = response.value("error", "unknown");
            throw std::runtime_error("Slack API error: " + error);
        }

        logger_->debug("Uploaded file to Slack channel {}: {}", channel_id, file_path);

    } catch (const std::exception& e) {
        logger_->error("Failed to upload file to Slack: {}", e.what());
        throw;
    }
}

void SlackChannel::AddReaction(const std::string& channel_id,
                              const std::string& timestamp,
                              const std::string& emoji) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    try {
        nlohmann::json request = {
            {"channel", channel_id},
            {"timestamp", timestamp},
            {"name", emoji}
        };

        std::string response_str = MakeApiRequest("reactions.add", "POST", request);
        auto response = nlohmann::json::parse(response_str);

        if (!response.contains("ok") || response["ok"] != true) {
            std::string error = response.value("error", "unknown");
            // Ignore "already_reacted" error
            if (error != "already_reacted") {
                throw std::runtime_error("Slack API error: " + error);
            }
        }

        logger_->debug("Added reaction {} to message in {}", emoji, channel_id);

    } catch (const std::exception& e) {
        logger_->error("Failed to add reaction: {}", e.what());
    }
}

std::string SlackChannel::GetUserInfo(const std::string& user_id) {
    try {
        nlohmann::json request = {
            {"user", user_id}
        };

        std::string response_str = MakeApiRequest("users.info", "POST", request);
        auto response = nlohmann::json::parse(response_str);

        if (response.contains("ok") && response["ok"] == true && response.contains("user")) {
            return response["user"].value("name", user_id);
        }

        return user_id;

    } catch (const std::exception& e) {
        logger_->error("Failed to get user info: {}", e.what());
        return user_id;
    }
}

void SlackChannel::SocketModeLoop() {
    logger_->info("Slack Socket Mode loop started");

    // Simplified implementation: poll for events using RTM API
    // In production, should use WebSocket connection to Socket Mode API

    while (running_) {
        try {
            // Poll for new events (simplified - real implementation would use WebSocket)
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // In a real implementation, this would:
            // 1. Connect to wss://wss.slack.com/link
            // 2. Receive events via WebSocket
            // 3. Acknowledge events
            // 4. Handle events

        } catch (const std::exception& e) {
            logger_->error("Error in Slack Socket Mode loop: {}", e.what());
        }
    }

    logger_->info("Slack Socket Mode loop stopped");
}

void SlackChannel::HandleEvent(const nlohmann::json& event) {
    try {
        if (!event.contains("type")) {
            return;
        }

        std::string event_type = event["type"];

        if (event_type == "message") {
            std::string channel = event.value("channel", "");
            std::string user = event.value("user", "");
            std::string text = event.value("text", "");
            std::string ts = event.value("ts", "");

            // Check if user is allowed
            if (!IsAllowed(user)) {
                logger_->warn("Ignoring message from unauthorized user: {}", user);
                return;
            }

            // Create message object
            ChannelMessage message;
            message.id = ts;
            message.sender_id = user;
            message.content = text;
            message.channel_id = channel;
            message.is_group_chat = true;

            // Call message handler
            if (message_handler_) {
                message_handler_(message);
            }

            logger_->debug("Received Slack message from {}: {}", user, text.substr(0, 50));
        }

    } catch (const std::exception& e) {
        logger_->error("Error handling Slack event: {}", e.what());
    }
}

bool SlackChannel::IsAllowed(const std::string& sender_id) const {
    // If no allowlist is configured, allow all
    if (config_.allowed_users.empty()) {
        return true;
    }

    // Check if user is in allowlist
    return std::find(config_.allowed_users.begin(),
                    config_.allowed_users.end(),
                    sender_id) != config_.allowed_users.end();
}

} // namespace quantclaw
