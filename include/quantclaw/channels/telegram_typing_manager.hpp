// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <chrono>

namespace quantclaw::channels {

/**
 * Telegram Typing 状态管理器
 *
 * 负责持续刷新 Telegram 的 "正在输入" 状态，确保长时间任务时状态不会超时。
 * Telegram API 的 typing 状态会在 5 秒后自动消失，因此需要每 4 秒刷新一次。
 *
 * 使用方法：
 * 1. 创建 TypingStateManager 实例
 * 2. 调用 Start() 开始刷新
 * 3. 任务完成后调用 Stop() 停止刷新
 *
 * 示例：
 * ```cpp
 * auto manager = std::make_shared<TypingStateManager>(
 *     chat_id,
 *     [this](const std::string& id) {
 *         this->SendChatAction(id, "typing");
 *     }
 * );
 * manager->Start();
 * // ... 执行长时间任务 ...
 * manager->Stop();
 * ```
 */
class TypingStateManager {
public:
    using SendActionCallback = std::function<void(const std::string& chat_id)>;

    /**
     * 构造函数
     * @param chat_id Telegram 聊天 ID
     * @param send_action_callback 发送 typing 状态的回调函数
     * @param refresh_interval_ms 刷新间隔（毫秒），默认 4000ms
     */
    TypingStateManager(const std::string& chat_id,
                       SendActionCallback send_action_callback,
                       int refresh_interval_ms = 4000);

    ~TypingStateManager();

    // 禁止拷贝和移动
    TypingStateManager(const TypingStateManager&) = delete;
    TypingStateManager& operator=(const TypingStateManager&) = delete;
    TypingStateManager(TypingStateManager&&) = delete;
    TypingStateManager& operator=(TypingStateManager&&) = delete;

    /**
     * 开始刷新 typing 状态
     * 立即发送一次 typing 状态，然后启动后台线程定期刷新
     */
    void Start();

    /**
     * 停止刷新 typing 状态
     * 停止后台线程并清理资源
     */
    void Stop();

    /**
     * 检查是否正在运行
     */
    bool IsRunning() const { return running_; }

    /**
     * 获取刷新次数（用于测试）
     */
    int GetRefreshCount() const { return refresh_count_; }

private:
    void refresh_loop();

    std::string chat_id_;
    SendActionCallback send_action_callback_;
    int refresh_interval_ms_;

    std::atomic<bool> running_{false};
    std::atomic<int> refresh_count_{0};
    std::thread refresh_thread_;
};

/**
 * RAII 包装器，用于自动管理 TypingStateManager 的生命周期
 *
 * 使用方法：
 * ```cpp
 * {
 *     TypingStateGuard guard(chat_id, [this](const std::string& id) {
 *         this->SendChatAction(id, "typing");
 *     });
 *     // ... 执行长时间任务 ...
 * } // guard 析构时自动停止 typing
 * ```
 */
class TypingStateGuard {
public:
    TypingStateGuard(const std::string& chat_id,
                     TypingStateManager::SendActionCallback send_action_callback,
                     int refresh_interval_ms = 4000)
        : manager_(std::make_shared<TypingStateManager>(
              chat_id, std::move(send_action_callback), refresh_interval_ms)) {
        manager_->Start();
    }

    ~TypingStateGuard() {
        if (manager_) {
            manager_->Stop();
        }
    }

    // 禁止拷贝和移动
    TypingStateGuard(const TypingStateGuard&) = delete;
    TypingStateGuard& operator=(const TypingStateGuard&) = delete;
    TypingStateGuard(TypingStateGuard&&) = delete;
    TypingStateGuard& operator=(TypingStateGuard&&) = delete;

    TypingStateManager* GetManager() { return manager_.get(); }

private:
    std::shared_ptr<TypingStateManager> manager_;
};

} // namespace quantclaw::channels
