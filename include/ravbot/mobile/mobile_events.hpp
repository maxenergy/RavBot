// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ravbot::mobile {

inline constexpr char kEventAssistantDelta[] = "assistant_delta";
inline constexpr char kEventAssistantFinal[] = "assistant_final";
inline constexpr char kEventToolStart[] = "tool_start";
inline constexpr char kEventToolResult[] = "tool_result";
inline constexpr char kEventMobileAsrPartial[] = "mobile.asr_partial";
inline constexpr char kEventMobileAsrFinal[] = "mobile.asr_final";
inline constexpr char kEventMobileTtsState[] = "mobile.tts_state";
inline constexpr char kEventMobileAvatarState[] = "mobile.avatar_state";
inline constexpr char kEventMobileRuntimeStatus[] = "mobile.runtime_status";
inline constexpr char kEventMobileDeviceStatus[] = "mobile.device_status";
inline constexpr char kEventMobileVisionObservation[] =
    "mobile.vision_observation";

enum class AvatarState {
  kIdle,
  kListen,
  kThink,
  kSpeak,
  kWatch,
  kError,
};

inline std::string AvatarStateToString(AvatarState state) {
  switch (state) {
    case AvatarState::kIdle:
      return "idle";
    case AvatarState::kListen:
      return "listen";
    case AvatarState::kThink:
      return "think";
    case AvatarState::kSpeak:
      return "speak";
    case AvatarState::kWatch:
      return "watch";
    case AvatarState::kError:
      return "error";
  }
  return "idle";
}

inline AvatarState AvatarStateFromString(const std::string& state) {
  if (state == "idle") return AvatarState::kIdle;
  if (state == "listen") return AvatarState::kListen;
  if (state == "think") return AvatarState::kThink;
  if (state == "speak") return AvatarState::kSpeak;
  if (state == "watch") return AvatarState::kWatch;
  if (state == "error") return AvatarState::kError;
  throw std::runtime_error("Unknown avatar state: " + state);
}

struct CameraFrame {
  int width = 0;
  int height = 0;
  std::string format = "rgba8888";
  int64_t timestamp_ms = 0;
  std::vector<uint8_t> data;
};

struct AsrUpdate {
  std::string text;
  bool is_final = false;
  uint64_t segment_index = 0;
  int sample_rate_hz = 0;
  int duration_ms = 0;
  double average_level = 0.0;
  double peak_level = 0.0;
  std::string end_reason;
};

struct DeviceStatusSnapshot {
  bool foreground = true;
  bool service_running = false;
  bool capture_requested = false;
  bool permissions_granted = false;
  bool host_web_search_enabled = true;
  bool host_web_fetch_enabled = true;
  bool host_haptics_enabled = true;
  std::string microphone_status = "stopped";
  std::string camera_status = "stopped";
  std::string speaker_status = "idle";
  int64_t timestamp_ms = 0;

  nlohmann::json ToJson() const {
    return {{"foreground", foreground},
            {"serviceRunning", service_running},
            {"captureRequested", capture_requested},
            {"permissionsGranted", permissions_granted},
            {"hostWebSearchEnabled", host_web_search_enabled},
            {"hostWebFetchEnabled", host_web_fetch_enabled},
            {"hostHapticsEnabled", host_haptics_enabled},
            {"microphoneStatus", microphone_status},
            {"cameraStatus", camera_status},
            {"speakerStatus", speaker_status},
            {"timestampMs", timestamp_ms}};
  }
};

struct MobileEvent {
  std::string name;
  nlohmann::json payload;

  nlohmann::json ToJson() const {
    return {{"event", name}, {"payload", payload}};
  }
};

}  // namespace ravbot::mobile
