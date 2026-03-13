// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/channels/line_channel.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

namespace quantclaw {

LineChannel::LineChannel(const std::string& channel_id,
                         const std::string& channel_access_token,
                         std::shared_ptr<spdlog::logger> logger)
    : channel_id_(channel_id),
      channel_access_token_(channel_access_token),
      logger_(logger) {}

LineChannel::~LineChannel() {
    Stop();
}

void LineChannel::Start() {
    if (running_) {
        logger_->warn("LINE channel already running");
        return;
    }

    running_ = true;
    receive_thread_ = std::thread(&LineChannel::ReceiveLoop, this);
    logger_->info("LINE channel started: {}", channel_id_);
}

void LineChannel::Stop() {
    if (!running_) return;

    running_ = false;
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    logger_->info("LINE channel stopped: {}", channel_id_);
}

void LineChannel::SendMessage(const std::string& recipient, const std::string& message) {
    try {
        nlohmann::json payload = {
            {"to", recipient},
            {"messages", nlohmann::json::array({
                {
                    {"type", "text"},
                    {"text", message}
                }
            })}
        };

        std::string response = MakeApiRequest("/v2/bot/message/push", "POST", payload.dump());
        logger_->info("LINE message sent to {}: {}", recipient, message.substr(0, 50));
    } catch (const std::exception& e) {
        logger_->error("Failed to send LINE message: {}", e.what());
    }
}

void LineChannel::SendReplyMessage(const std::string& reply_token, const std::string& message) {
    try {
        nlohmann::json payload = {
            {"replyToken", reply_token},
            {"messages", nlohmann::json::array({
                {
                    {"type", "text"},
                    {"text", message}
                }
            })}
        };

        std::string response = MakeApiRequest("/v2/bot/message/reply", "POST", payload.dump());
        logger_->info("LINE reply sent: {}", message.substr(0, 50));
    } catch (const std::exception& e) {
        logger_->error("Failed to send LINE reply: {}", e.what());
    }
}

bool LineChannel::IsAllowed(const std::string& sender) const {
    // TODO: Implement whitelist check
    return true;
}

void LineChannel::ReceiveLoop() {
    logger_->info("LINE receive loop started");

    while (running_) {
        // TODO: Implement webhook server
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    logger_->info("LINE receive loop stopped");
}

std::string LineChannel::MakeApiRequest(const std::string& endpoint,
                                        const std::string& method,
                                        const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string url = "https://api.line.me" + endpoint;
    std::string response_data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth_header = "Authorization: Bearer " + channel_access_token_;
    headers = curl_slist_append(headers, auth_header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
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
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
    }

    return response_data;
}

} // namespace quantclaw
