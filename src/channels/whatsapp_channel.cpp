// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/channels/whatsapp_channel.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

namespace ravbot {

WhatsAppChannel::WhatsAppChannel(const std::string& channel_id,
                                 const std::string& api_key,
                                 const std::string& phone_number_id,
                                 std::shared_ptr<spdlog::logger> logger)
    : channel_id_(channel_id),
      api_key_(api_key),
      phone_number_id_(phone_number_id),
      logger_(logger) {}

WhatsAppChannel::~WhatsAppChannel() {
    Stop();
}

void WhatsAppChannel::Start() {
    if (running_) {
        logger_->warn("WhatsApp channel already running");
        return;
    }

    running_ = true;
    receive_thread_ = std::thread(&WhatsAppChannel::ReceiveLoop, this);
    logger_->info("WhatsApp channel started: {}", channel_id_);
}

void WhatsAppChannel::Stop() {
    if (!running_) return;

    running_ = false;
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    logger_->info("WhatsApp channel stopped: {}", channel_id_);
}

void WhatsAppChannel::SendMessage(const std::string& recipient, const std::string& message) {
    try {
        nlohmann::json payload = {
            {"messaging_product", "whatsapp"},
            {"to", recipient},
            {"type", "text"},
            {"text", {{"body", message}}}
        };

        std::string endpoint = "/" + phone_number_id_ + "/messages";
        std::string response = MakeApiRequest(endpoint, "POST", payload.dump());

        logger_->info("WhatsApp message sent to {}: {}", recipient, message.substr(0, 50));
    } catch (const std::exception& e) {
        logger_->error("Failed to send WhatsApp message: {}", e.what());
    }
}

bool WhatsAppChannel::IsAllowed(const std::string& sender) const {
    // TODO: Implement whitelist check
    return true;
}

void WhatsAppChannel::ReceiveLoop() {
    logger_->info("WhatsApp receive loop started");

    while (running_) {
        // TODO: Implement webhook polling or webhook server
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    logger_->info("WhatsApp receive loop stopped");
}

std::string WhatsAppChannel::MakeApiRequest(const std::string& endpoint,
                                            const std::string& method,
                                            const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string url = "https://graph.facebook.com/v18.0" + endpoint;
    std::string response_data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth_header = "Authorization: Bearer " + api_key_;
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

} // namespace ravbot
