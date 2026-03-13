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

// iMessage channel adapter (macOS only)
// Requires AppleScript or Messages.app integration
class IMessageChannel : public Channel {
public:
    IMessageChannel(const std::string& channel_id,
                    std::shared_ptr<spdlog::logger> logger);
    ~IMessageChannel() override;

    void Start() override;
    void Stop() override;
    std::string GetChannelName() const override { return "imessage"; }

    void SendMessage(const std::string& recipient, const std::string& message) override;
    bool IsAllowed(const std::string& sender) const override;

private:
    std::string channel_id_;
    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<bool> running_{false};
    std::thread receive_thread_;

    void ReceiveLoop();
    bool SendViaAppleScript(const std::string& recipient, const std::string& message);
};

} // namespace quantclaw
