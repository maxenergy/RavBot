// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/channels/imessage_channel.hpp"
#include <chrono>
#include <thread>

namespace ravbot {

IMessageChannel::IMessageChannel(const std::string& channel_id,
                                 std::shared_ptr<spdlog::logger> logger)
    : channel_id_(channel_id), logger_(logger) {}

IMessageChannel::~IMessageChannel() {
    Stop();
}

void IMessageChannel::Start() {
    if (running_) {
        logger_->warn("iMessage channel already running");
        return;
    }

    running_ = true;
    receive_thread_ = std::thread(&IMessageChannel::ReceiveLoop, this);
    logger_->info("iMessage channel started: {}", channel_id_);
}

void IMessageChannel::Stop() {
    if (!running_) return;

    running_ = false;
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    logger_->info("iMessage channel stopped: {}", channel_id_);
}

void IMessageChannel::SendMessage(const std::string& recipient, const std::string& message) {
#ifdef __APPLE__
    if (SendViaAppleScript(recipient, message)) {
        logger_->info("iMessage sent to {}: {}", recipient, message.substr(0, 50));
    } else {
        logger_->error("Failed to send iMessage to {}", recipient);
    }
#else
    logger_->warn("iMessage is only supported on macOS");
#endif
}

bool IMessageChannel::IsAllowed(const std::string& sender) const {
    // TODO: Implement whitelist check
    return true;
}

void IMessageChannel::ReceiveLoop() {
    logger_->info("iMessage receive loop started");

    while (running_) {
        // TODO: Implement message polling via AppleScript or Messages.app database
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    logger_->info("iMessage receive loop stopped");
}

bool IMessageChannel::SendViaAppleScript(const std::string& recipient, const std::string& message) {
#ifdef __APPLE__
    // Escape quotes in message
    std::string escaped_message = message;
    size_t pos = 0;
    while ((pos = escaped_message.find("\"", pos)) != std::string::npos) {
        escaped_message.replace(pos, 1, "\\\"");
        pos += 2;
    }

    // Build AppleScript command
    std::string script = "osascript -e 'tell application \"Messages\" to send \"" +
                        escaped_message + "\" to buddy \"" + recipient + "\"'";

    int result = system(script.c_str());
    return result == 0;
#else
    return false;
#endif
}

} // namespace ravbot
