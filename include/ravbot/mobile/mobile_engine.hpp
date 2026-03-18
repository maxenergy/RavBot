// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#include "ravbot/config.hpp"
#include "ravbot/core/memory_search.hpp"
#include "ravbot/mobile/device_capability_bridge.hpp"
#include "ravbot/mobile/mobile_events.hpp"
#include "ravbot/providers/llm_provider.hpp"
#include "ravbot/session/session_manager.hpp"

namespace ravbot::mobile {

class SpeechPipeline;

class MobileAsrProvider {
 public:
  virtual ~MobileAsrProvider() = default;

  virtual AsrUpdate PushPcm16(const int16_t* samples,
                              size_t sample_count,
                              int sample_rate_hz,
                              bool end_of_turn) = 0;
  virtual AsrUpdate Flush() = 0;
  virtual void Interrupt() = 0;
};

class MobileVisionProvider {
 public:
  virtual ~MobileVisionProvider() = default;

  virtual std::optional<std::string> ObserveFrame(
      const CameraFrame& frame) = 0;
};

class MobileEngine {
 public:
  using EventCallback = std::function<void(const MobileEvent&)>;

  MobileEngine(RavBotConfig config,
               const std::filesystem::path& state_dir,
               const std::filesystem::path& models_dir,
               std::shared_ptr<spdlog::logger> logger);

  void SetTextProvider(std::shared_ptr<LLMProvider> provider);
  void SetAsrProvider(std::shared_ptr<MobileAsrProvider> provider);
  void SetVisionProvider(std::shared_ptr<MobileVisionProvider> provider);
  void SetDeviceBridge(std::shared_ptr<DeviceCapabilityBridge> bridge);

  std::string SubscribeEvents(EventCallback callback);
  void UnsubscribeEvents(const std::string& subscription_id);

  std::string StartSession(const std::string& session_key,
                           const std::string& display_name = "");
  bool SendTextTurn(const std::string& session_key, const std::string& text);
  bool PushPcm16(const std::string& session_key,
                 const int16_t* samples,
                 size_t sample_count,
                 int sample_rate_hz,
                 bool end_of_turn);
  bool FlushAudioTurn(const std::string& session_key);
  bool PushCameraFrame(const std::string& session_key,
                       const CameraFrame& frame);

  void InterruptGeneration(const std::string& session_key);
  bool ReportTtsPlaybackState(const std::string& session_key,
                              const std::string& state);
  bool ReportDeviceStatus(DeviceStatusSnapshot status);
  void SetForegroundState(bool foreground);
  bool IsForeground() const;

  const RavBotConfig& config() const { return config_; }
  SessionManager& session_manager() { return session_manager_; }

 private:
  void Emit(const std::string& event_name, const nlohmann::json& payload) const;
  void EmitRuntimeStatus() const;
  void SetAvatarState(AvatarState state);
  bool ShouldProcessVisionFrame(const CameraFrame& frame);
  std::optional<double> EstimateFrameSceneSignature(
      const CameraFrame& frame) const;
  bool HandleAsrUpdate(const std::string& session_key,
                       const AsrUpdate& update,
                       int sample_rate_hz);
  std::vector<nlohmann::json> BuildToolSchemas() const;
  std::string ExecuteToolCall(const ToolCall& tool_call) const;
  std::string BuildDeviceStatusToolResult() const;
  std::string BuildCameraSnapshotToolResult() const;
  std::string BuildTimeToolResult() const;
  std::string BuildWebSearchToolResult(const nlohmann::json& arguments) const;
  std::string BuildWebFetchToolResult(const nlohmann::json& arguments) const;
  std::string BuildMemoryListToolResult(const nlohmann::json& arguments) const;
  std::string BuildMemorySearchToolResult(const nlohmann::json& arguments) const;
  std::string BuildMemoryGetToolResult(const nlohmann::json& arguments) const;
  std::string BuildMemoryWriteToolResult(const nlohmann::json& arguments) const;
  std::string BuildMemoryDeleteToolResult(
      const nlohmann::json& arguments) const;
  std::filesystem::path WorkspaceRoot() const;
  std::filesystem::path ResolveWorkspacePath(
      const std::string& relative_path) const;
  std::shared_ptr<DeviceCapabilityBridge> CopyDeviceBridge() const;
  bool HandleUserTextTurn(const std::string& session_key,
                          const std::string& text,
                          bool emit_asr_final);
  std::vector<Message> BuildRequestMessages(const std::string& session_key) const;
  std::string NextSubscriptionId();

  RavBotConfig config_;
  std::shared_ptr<spdlog::logger> logger_;
  SessionManager session_manager_;
  std::filesystem::path state_dir_;
  std::filesystem::path models_dir_;
  std::shared_ptr<LLMProvider> text_provider_;
  std::shared_ptr<MobileAsrProvider> asr_provider_;
  std::shared_ptr<SpeechPipeline> speech_pipeline_;
  std::shared_ptr<MobileVisionProvider> vision_provider_;
  std::shared_ptr<DeviceCapabilityBridge> device_bridge_;

  mutable std::shared_mutex state_mutex_;
  bool foreground_ = true;
  AvatarState avatar_state_ = AvatarState::kIdle;
  DeviceStatusSnapshot device_status_;
  bool has_device_status_ = false;
  nlohmann::json last_vision_observation_ = nlohmann::json::object();
  bool has_last_vision_observation_ = false;
  int64_t last_vision_timestamp_ms_ = 0;
  std::optional<double> last_vision_scene_signature_;

  mutable std::shared_mutex subscriber_mutex_;
  std::unordered_map<std::string, EventCallback> subscribers_;
  uint64_t next_subscription_id_ = 1;
};

}  // namespace ravbot::mobile
