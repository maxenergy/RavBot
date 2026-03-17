// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/channels/telegram_typing_manager.hpp"
#include <spdlog/spdlog.h>

namespace ravbot::channels {

TypingStateManager::TypingStateManager(const std::string& chat_id,
                                       SendActionCallback send_action_callback,
                                       int refresh_interval_ms)
    : chat_id_(chat_id),
      send_action_callback_(std::move(send_action_callback)),
      refresh_interval_ms_(refresh_interval_ms) {
    // 验证参数
    if (refresh_interval_ms_ < 1000) {
        spdlog::warn("TypingStateManager: refresh_interval_ms too small ({}ms), using 1000ms",
                     refresh_interval_ms_);
        refresh_interval_ms_ = 1000;
    }
    if (refresh_interval_ms_ > 10000) {
        spdlog::warn("TypingStateManager: refresh_interval_ms too large ({}ms), using 10000ms",
                     refresh_interval_ms_);
        refresh_interval_ms_ = 10000;
    }
}

TypingStateManager::~TypingStateManager() {
    Stop();
}

void TypingStateManager::Start() {
    if (running_) {
        spdlog::warn("TypingStateManager: already running for chat_id={}", chat_id_);
        return;
    }

    // 立即发送一次 typing 状态
    try {
        send_action_callback_(chat_id_);
        refresh_count_++;
    } catch (const std::exception& e) {
        spdlog::error("TypingStateManager: failed to send initial typing action: {}", e.what());
    }

    // 启动后台刷新线程
    running_ = true;
    refresh_thread_ = std::thread([this]() {
        refresh_loop();
    });

    spdlog::debug("TypingStateManager: started for chat_id={}, interval={}ms",
                  chat_id_, refresh_interval_ms_);
}

void TypingStateManager::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (refresh_thread_.joinable()) {
        refresh_thread_.join();
    }

    spdlog::debug("TypingStateManager: stopped for chat_id={}, total_refreshes={}",
                  chat_id_, refresh_count_.load());
}

void TypingStateManager::refresh_loop() {
    while (running_) {
        // 等待刷新间隔
        auto start = std::chrono::steady_clock::now();
        while (running_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            ).count();

            if (elapsed >= refresh_interval_ms_) {
                break;
            }

            // 每 100ms 检查一次是否需要停止
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!running_) {
            break;
        }

        // 发送 typing 状态
        try {
            send_action_callback_(chat_id_);
            refresh_count_++;
            spdlog::trace("TypingStateManager: refreshed typing for chat_id={}, count={}",
                          chat_id_, refresh_count_.load());
        } catch (const std::exception& e) {
            spdlog::error("TypingStateManager: failed to refresh typing action: {}", e.what());
            // 继续运行，不因为单次失败而停止
        }
    }
}

} // namespace ravbot::channels
