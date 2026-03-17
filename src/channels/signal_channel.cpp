// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/channels/signal_channel.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <algorithm>

namespace ravbot {

SignalChannel::SignalChannel(const SignalConfig& config, std::shared_ptr<spdlog::logger> logger)
    : config_(config), logger_(logger) {}

SignalChannel::~SignalChannel() {
    Stop();
}

bool SignalChannel::CheckSignalCli() {
    try {
        // Try to ping the signal-cli REST API
        auto response = MakeApiRequest("/v1/about", "GET");
        logger_->info("Signal CLI is available");
        return true;
    } catch (const std::exception& e) {
        logger_->error("Signal CLI not available: {}", e.what());
        return false;
    }
}

void SignalChannel::Start() {
    if (running_) {
        logger_->warn("Signal channel already running");
        return;
    }

    if (!CheckSignalCli()) {
        throw std::runtime_error("Signal CLI is not available. Please start signal-cli daemon.");
    }

    running_ = true;
    receive_thread_ = std::thread(&SignalChannel::ReceiveLoop, this);
    logger_->info("Signal channel started for account: {}", config_.account);
}

void SignalChannel::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    logger_->info("Signal channel stopped");
}

std::string SignalChannel::MakeApiRequest(const std::string& endpoint,
                                         const std::string& method,
                                         const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string url = config_.base_url + endpoint;
    std::string response_data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config_.timeout_ms);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        }
    } else if (method == "GET") {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

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

    if (http_code != 200 && http_code != 201) {
        throw std::runtime_error("Signal API returned HTTP " + std::to_string(http_code) + ": " + response_data);
    }

    return response_data;
}

void SignalChannel::SendMessage(const std::string& recipient, const std::string& message) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    try {
        nlohmann::json request = {
            {"message", message},
            {"number", config_.account},
            {"recipients", nlohmann::json::array({recipient})}
        };

        std::string endpoint = "/v2/send";
        std::string response = MakeApiRequest(endpoint, "POST", request.dump());

        logger_->debug("Sent Signal message to {}: {}", recipient, message.substr(0, 50));

    } catch (const std::exception& e) {
        logger_->error("Failed to send Signal message: {}", e.what());
        throw;
    }
}

void SignalChannel::SendGroupMessage(const std::string& group_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    try {
        nlohmann::json request = {
            {"message", message},
            {"number", config_.account},
            {"groupId", group_id}
        };

        std::string endpoint = "/v2/send";
        std::string response = MakeApiRequest(endpoint, "POST", request.dump());

        logger_->debug("Sent Signal group message to {}: {}", group_id, message.substr(0, 50));

    } catch (const std::exception& e) {
        logger_->error("Failed to send Signal group message: {}", e.what());
        throw;
    }
}

void SignalChannel::SendAttachment(const std::string& recipient, const std::string& file_path) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    try {
        nlohmann::json request = {
            {"message", ""},
            {"number", config_.account},
            {"recipients", nlohmann::json::array({recipient})},
            {"attachments", nlohmann::json::array({file_path})}
        };

        std::string endpoint = "/v2/send";
        std::string response = MakeApiRequest(endpoint, "POST", request.dump());

        logger_->debug("Sent Signal attachment to {}: {}", recipient, file_path);

    } catch (const std::exception& e) {
        logger_->error("Failed to send Signal attachment: {}", e.what());
        throw;
    }
}

void SignalChannel::ReceiveLoop() {
    logger_->info("Signal receive loop started");

    while (running_) {
        try {
            // Poll for new messages
            std::string endpoint = "/v1/receive/" + config_.account;
            std::string response = MakeApiRequest(endpoint, "GET");

            if (!response.empty() && response != "[]") {
                auto messages = nlohmann::json::parse(response);

                for (const auto& msg_json : messages) {
                    if (!msg_json.contains("envelope")) {
                        continue;
                    }

                    const auto& envelope = msg_json["envelope"];
                    if (!envelope.contains("dataMessage")) {
                        continue;
                    }

                    const auto& data_msg = envelope["dataMessage"];
                    std::string sender = envelope.value("source", "");
                    std::string content = data_msg.value("message", "");
                    std::string group_id = data_msg.value("groupInfo", nlohmann::json::object()).value("groupId", "");

                    if (content.empty()) {
                        continue;
                    }

                    // Check if sender is allowed
                    if (!IsAllowed(sender)) {
                        logger_->warn("Ignoring message from unauthorized sender: {}", sender);
                        continue;
                    }

                    // Create message object
                    ChannelMessage message;
                    message.id = std::to_string(envelope.value("timestamp", 0));
                    message.sender_id = sender;
                    message.content = content;
                    message.channel_id = group_id.empty() ? sender : group_id;
                    message.is_group_chat = !group_id.empty();

                    // Call message handler
                    if (message_handler_) {
                        message_handler_(message);
                    }

                    logger_->debug("Received Signal message from {}: {}", sender, content.substr(0, 50));
                }
            }

        } catch (const std::exception& e) {
            logger_->error("Error in Signal receive loop: {}", e.what());
        }

        // Sleep for a bit before polling again
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    logger_->info("Signal receive loop stopped");
}

bool SignalChannel::IsAllowed(const std::string& sender_id) const {
    // If no allowlist is configured, allow all
    if (config_.allowed_senders.empty()) {
        return true;
    }

    // Check if sender is in allowlist
    return std::find(config_.allowed_senders.begin(),
                    config_.allowed_senders.end(),
                    sender_id) != config_.allowed_senders.end();
}

} // namespace ravbot
