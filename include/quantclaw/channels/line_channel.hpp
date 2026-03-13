// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "quantclaw/channels/channel_base.hpp"
#include <spdlog/spdlog.h>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

namespace quantclaw {

// LINE Messaging API channel adapter
class LineChannel : public Channel {
public:
    LineChannel(const std::string& channel_id,
                const std::string& channel_access_token,
                std::shared_ptr<spdlog::logger> logger);
    ~LineChannel() override;

    void Start() override;
    void Stop() override;
    std::string GetChannelName() const override { return "line"; }

    void SendMessage(const std::string& recipient, const std::string& message) override;
    void SendReplyMessage(const std::string& reply_token, const std::string& message);
    bool IsAllowed(const std::string& sender) const override;

private:
    std::string channel_id_;
    std::string channel_access_token_;
    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<bool> running_{false};
    std::thread receive_thread_;

    void ReceiveLoop();
    std::string MakeApiRequest(const std::string& endpoint, const std::string& method,
                               const std::string& body = "");
};

} // namespace quantclaw
