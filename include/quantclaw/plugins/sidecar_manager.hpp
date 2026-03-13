// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "quantclaw/platform/process.hpp"
#include "quantclaw/platform/ipc.hpp"

namespace quantclaw {

// JSON-RPC 2.0 request/response for IPC with Node.js sidecar
struct SidecarRequest {
  std::string method;
  nlohmann::json params;
  int id = 0;

  nlohmann::json to_json() const;
};

struct SidecarResponse {
  int id = 0;
  nlohmann::json result;
  std::string error;
  bool ok = true;

  static SidecarResponse FromJson(const nlohmann::json& j);
};

// Sidecar 状态枚举 (Requirements: 19.8)
enum class SidecarState {
  kStopped,    // 已停止
  kStarting,   // 启动中
  kRunning,    // 运行中
  kRestarting, // 重启中
  kFailed      // 失败（达到最大重启次数）
};

// Sidecar 状态信息 (Requirements: 19.8)
struct SidecarStatus {
  SidecarState state;
  platform::ProcessId pid;
  int restart_count;
  int max_restarts;
  std::string last_error;
  std::chrono::system_clock::time_point last_restart_time;
  std::chrono::milliseconds uptime;
};

// Manages the Node.js sidecar subprocess lifecycle.
// nginx-style: fork/exec, heartbeat, graceful reload/stop, crash restart.
class SidecarManager {
 public:
  explicit SidecarManager(std::shared_ptr<spdlog::logger> logger);
  ~SidecarManager();

  SidecarManager(const SidecarManager&) = delete;
  SidecarManager& operator=(const SidecarManager&) = delete;

  struct Options {
    std::string node_binary = "node";
    std::string sidecar_script;  // path to sidecar entry point
    std::string pid_file;
    int heartbeat_interval_ms = 5000;
    int heartbeat_timeout_count = 3;  // miss count before declaring dead
    int graceful_stop_timeout_ms = 10000;
    int max_restarts = 10;
    std::vector<std::string> env_whitelist;
    nlohmann::json plugin_config;  // passed to sidecar as startup config

    // Deprecated: ignored. TCP port is assigned dynamically by the OS.
    std::string socket_path;
  };

  // Start the sidecar process
  bool Start(const Options& opts);

  // Stop the sidecar gracefully
  void Stop();

  // Reload plugins (SIGHUP to sidecar on Unix, no-op on Windows)
  bool Reload();

  // Send a JSON-RPC request and wait for response
  SidecarResponse Call(const std::string& method,
                       const nlohmann::json& params,
                       int timeout_ms = 30000);

  // Check if sidecar is alive
  bool IsRunning() const;

  // Get sidecar PID
  platform::ProcessId pid() const { return pid_; }

  // Get detailed sidecar status (Requirements: 19.8)
  SidecarStatus GetStatus() const;

  // Set maximum restart attempts (Requirements: 19.4)
  void SetMaxRestartAttempts(int max_restarts);

  // Get restart count
  int GetRestartCount() const { return restart_count_; }

 private:
  void monitor_loop();
  bool spawn_sidecar();
  void kill_sidecar(bool force = false);
  bool connect_ipc();
  void write_pid_file();
  void remove_pid_file();
  int next_backoff_ms();

  std::shared_ptr<spdlog::logger> logger_;
  Options opts_;

  std::atomic<platform::ProcessId> pid_{platform::kInvalidPid};
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};

  platform::IpcHandle ipc_handle_ = platform::kInvalidIpc;
  int ipc_port_ = 0;  // TCP port assigned after IpcServer::listen()
  std::mutex ipc_mu_;

  std::thread monitor_thread_;
  std::atomic<int> restart_count_{0};
  std::chrono::steady_clock::time_point last_restart_;
  std::chrono::steady_clock::time_point start_time_;  // 启动时间
  std::string last_error_;  // 最后错误信息
  mutable std::mutex status_mu_;  // 保护状态信息

  std::atomic<int> rpc_id_{1};
};

}  // namespace quantclaw
