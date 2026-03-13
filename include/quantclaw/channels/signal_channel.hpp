// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "quantclaw/channels/channel_base.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <atomic>
#include <mutex>

namespace quantclaw {

struct SignalConfig {
    std::string signal_cli_path = "signal-cli";
    std::string account;  // Phone number or username
    std::string base_url = "http://localhost:8080";  // signal-cli REST API
    int timeout_ms = 30000;
    std::vector<std::string> allowed_senders;
    std::vector<std::string> allowed_groups;
};

class SignalChannel : public Channel {
public:
    SignalChannel(const SignalConfig& config, std::shared_ptr<spdlog::logger> logger);
    ~SignalChannel() override;

    void Start() override;
    void Stop() override;
    void SendMessage(const std::string& recipient, const std::string& message) override;
    bool IsAllowed(const std::string& sender_id) const override;
    std::string GetChannelName() const override { return "signal"; }

    // Signal-specific methods
    void SendGroupMessage(const std::string& group_id, const std::string& message);
    void SendAttachment(const std::string& recipient, const std::string& file_path);

private:
    void ReceiveLoop();
    std::string MakeApiRequest(const std::string& endpoint, const std::string& method, const std::string& body = "");
    bool CheckSignalCli();

    SignalConfig config_;
    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<bool> running_{false};
    std::thread receive_thread_;
    std::mutex send_mutex_;
};

} // namespace quantclaw
