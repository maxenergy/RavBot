// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ixwebsocket/IXWebSocket.h>
#include "quantclaw/platform/process.hpp"

namespace quantclaw {

// Browser connection info
struct BrowserConnection {
  std::string cdp_url;       // Chrome DevTools Protocol WebSocket URL
  std::string pid_or_id;     // Browser PID or container ID
  bool is_remote = false;
  std::atomic<bool> is_running{false};

  BrowserConnection() = default;
  BrowserConnection(const BrowserConnection&) = delete;
  BrowserConnection& operator=(const BrowserConnection&) = delete;
};

// Browser page state
struct PageState {
  std::string url;
  std::string title;
  std::vector<std::string> console_messages;
  std::vector<std::string> errors;
  int request_count = 0;
};

// SSRF policy for navigation
struct SsrfPolicy {
  std::vector<std::string> blocked_hosts;   // e.g. ["localhost", "127.0.0.1"]
  std::vector<std::string> blocked_ranges;  // e.g. ["10.0.0.0/8", "192.168.0.0/16"]
  std::vector<std::string> allowed_hosts;   // If non-empty, only these allowed

  bool is_allowed(const std::string& host) const;

  static SsrfPolicy default_policy();
};

// Browser tool configuration
struct BrowserToolConfig {
  // How to get a browser
  enum class Mode {
    kLocal,    // Spawn local Chromium/Chrome
    kRemote,   // Connect to remote browser via CDP
  };

  Mode mode = Mode::kLocal;
  std::string chromium_path;      // Path to chromium binary (auto-detect if empty)
  std::string remote_cdp_url;     // CDP URL for remote mode
  bool headless = true;
  int viewport_width = 1280;
  int viewport_height = 720;
  int navigation_timeout_ms = 30000;
  // Local DevTools port (used when mode=kLocal).
  // WARNING: Only one BrowserSession per port is supported at a time.
  // If running multiple sessions concurrently, each must use a unique port.
  int cdp_debug_port = 9222;
  SsrfPolicy ssrf_policy;

  // Convenience factory methods
  static BrowserToolConfig Default() {
    return BrowserToolConfig{};
  }

  static BrowserToolConfig Headless() {
    BrowserToolConfig c;
    c.headless = true;
    return c;
  }

  static BrowserToolConfig WithViewport(int width, int height) {
    BrowserToolConfig c;
    c.viewport_width = width;
    c.viewport_height = height;
    return c;
  }

  static BrowserToolConfig FromJson(const nlohmann::json& j);
};

// Browser session: manages a browser instance and page interactions
class BrowserSession {
 public:
  explicit BrowserSession(std::shared_ptr<spdlog::logger> logger);
  ~BrowserSession();

  // Initialize browser (launch or connect)
  bool initialize(const BrowserToolConfig& config);

  // Simplified initialization with default config
  bool initialize() {
    return initialize(BrowserToolConfig::Default());
  }

  // Close browser
  void close();

  // Navigation
  bool navigate(const std::string& url);
  std::string current_url() const;
  std::string page_title() const;

  // Page interaction (via CDP)
  std::string evaluate_js(const std::string& expression);
  bool click(const std::string& selector);
  bool type(const std::string& selector, const std::string& text);

  // Screenshots
  std::string screenshot_base64(bool full_page = false);

  // State
  PageState get_state() const;
  bool is_connected() const;

  // Get connection info
  const BrowserConnection& connection() const { return connection_; }

  // Session metadata
  std::string session_id() const { return session_id_; }
  void set_session_id(const std::string& id) { session_id_ = id; }
  std::chrono::system_clock::time_point created_at() const { return created_at_; }
  std::chrono::system_clock::time_point last_used_at() const { return last_used_at_; }
  void update_last_used() { last_used_at_ = std::chrono::system_clock::now(); }

 private:
  std::shared_ptr<spdlog::logger> logger_;
  BrowserToolConfig config_;
  BrowserConnection connection_;
  PageState state_;
  mutable std::mutex mu_;
  platform::ProcessId browser_pid_ = platform::kInvalidPid;

  // Session metadata
  std::string session_id_;
  std::chrono::system_clock::time_point created_at_;
  std::chrono::system_clock::time_point last_used_at_;

  // CDP WebSocket connection
  ix::WebSocket cdp_ws_;
  std::atomic<int> cdp_id_{0};
  mutable std::mutex cdp_mu_;
  std::condition_variable cdp_cv_;
  std::unordered_map<int, nlohmann::json> cdp_responses_;
  std::set<int> cdp_pending_ids_;  // IDs awaiting a response

  // Launch local browser
  bool launch_local();
  // Connect to remote CDP
  bool connect_remote();
  // Establish WebSocket CDP connection to ws_url
  bool connect_cdp_websocket(const std::string& ws_url);

  // CDP communication (sends over WebSocket, waits for response)
  std::string cdp_send(const std::string& method,
                        const nlohmann::json& params = {});

  // Find chromium binary
  static std::string find_chromium();

  // Check SSRF policy
  bool check_navigation(const std::string& url) const;
};

// Session info for listing
struct SessionInfo {
  std::string session_id;
  std::string current_url;
  std::string page_title;
  bool is_connected;
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point last_used_at;
};

// Session lifecycle configuration
struct SessionLifecycleConfig {
  int idle_timeout_seconds = 300;        // 5 minutes idle timeout
  int max_lifetime_seconds = 3600;       // 1 hour max lifetime
  int health_check_interval_seconds = 30; // Health check every 30 seconds
  bool auto_cleanup = true;              // Auto cleanup expired sessions
};

// Browser session manager: manages multiple browser sessions
class BrowserSessionManager {
 public:
  explicit BrowserSessionManager(std::shared_ptr<spdlog::logger> logger);
  explicit BrowserSessionManager(std::shared_ptr<spdlog::logger> logger,
                                  const SessionLifecycleConfig& lifecycle_config);
  ~BrowserSessionManager();

  // Create a new session
  std::string create_session(const BrowserToolConfig& config);

  // Get a session by ID
  std::shared_ptr<BrowserSession> get_session(const std::string& session_id);

  // List all sessions
  std::vector<SessionInfo> list_sessions() const;

  // Close a session
  bool close_session(const std::string& session_id);

  // Close all sessions
  void close_all_sessions();

  // Get or create default session
  std::shared_ptr<BrowserSession> get_or_create_default(const BrowserToolConfig& config);

  // Lifecycle management
  bool validate_session(const std::string& session_id);
  void cleanup_expired_sessions();
  void start_lifecycle_manager();
  void stop_lifecycle_manager();

 private:
  std::shared_ptr<spdlog::logger> logger_;
  mutable std::mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<BrowserSession>> sessions_;
  int next_session_id_ = 1;
  SessionLifecycleConfig lifecycle_config_;

  // Lifecycle management
  std::atomic<bool> lifecycle_running_{false};
  std::thread lifecycle_thread_;

  std::string generate_session_id();
  void lifecycle_loop();
  bool is_session_expired(const std::shared_ptr<BrowserSession>& session) const;
  bool is_session_idle(const std::shared_ptr<BrowserSession>& session) const;
};

// Browser tool functions for registration with ToolRegistry
namespace browser_tools {

// Create browser tool schemas for LLM function calling
std::vector<nlohmann::json> get_tool_schemas();

// Create a browser tool executor
std::function<std::string(const nlohmann::json&)>
create_executor(std::shared_ptr<BrowserSession> session);

}  // namespace browser_tools

}  // namespace quantclaw
