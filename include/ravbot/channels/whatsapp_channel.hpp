// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ravbot/channels/channel_base.hpp"
#include <spdlog/spdlog.h>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

namespace ravbot {

// WhatsApp Business API channel adapter
class WhatsAppChannel : public Channel {
public:
    WhatsAppChannel(const std::string& channel_id,
                    const std::string& api_key,
                    const std::string& phone_number_id,
                    std::shared_ptr<spdlog::logger> logger);
    ~WhatsAppChannel() override;

    void Start() override;
    void Stop() override;
    std::string GetChannelName() const override { return "whatsapp"; }

    void SendMessage(const std::string& recipient, const std::string& message) override;
    bool IsAllowed(const std::string& sender) const override;

private:
    std::string channel_id_;
    std::string api_key_;
    std::string phone_number_id_;
    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<bool> running_{false};
    std::thread receive_thread_;

    void ReceiveLoop();
    std::string MakeApiRequest(const std::string& endpoint, const std::string& method,
                               const std::string& body = "");
};

} // namespace ravbot
