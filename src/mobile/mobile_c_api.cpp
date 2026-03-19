// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/mobile/mobile_c_api.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "ravbot/mobile/device_capability_bridge.hpp"
#include "ravbot/mobile/mobile_engine.hpp"

namespace ravbot::mobile {
class CallbackDeviceBridge : public DeviceCapabilityBridge {
 public:
  CallbackDeviceBridge(std::string state_dir, std::string models_dir,
                       ravbot_mobile_device_callbacks_t callbacks,
                       void* user_data)
      : state_dir_(std::move(state_dir)),
        models_dir_(std::move(models_dir)),
        callbacks_(callbacks),
        user_data_(user_data) {}

  std::string ResolveStateDirectory() const override {
    return state_dir_;
  }
  std::string ResolveModelsDirectory() const override {
    return models_dir_;
  }
  bool IsForeground() const override {
    return foreground_;
  }
  bool SupportsWebSearch() const override {
    return callbacks_.on_web_search != nullptr;
  }
  bool SupportsWebFetch() const override {
    return callbacks_.on_web_fetch != nullptr;
  }
  bool SupportsCaptureControl() const override {
    return callbacks_.on_capture_control != nullptr;
  }
  bool SupportsVibration() const override {
    return callbacks_.on_vibrate != nullptr;
  }

  void SetAvatarState(const std::string& session_key,
                      AvatarState state) override {
    if (callbacks_.on_avatar_state == nullptr) {
      return;
    }
    const std::string state_name = AvatarStateToString(state);
    callbacks_.on_avatar_state(session_key.c_str(), state_name.c_str(),
                               user_data_);
  }

  void RequestSpeechPlayback(const std::string& session_key,
                             const std::string& text) override {
    if (callbacks_.on_speech_request == nullptr) {
      return;
    }
    callbacks_.on_speech_request(session_key.c_str(), text.c_str(), user_data_);
  }

  void InterruptSpeechPlayback(const std::string& session_key) override {
    if (callbacks_.on_speech_interrupt == nullptr) {
      return;
    }
    callbacks_.on_speech_interrupt(session_key.c_str(), user_data_);
  }

  std::string SetCaptureEnabled(const std::string& session_key,
                                bool enabled) override {
    if (callbacks_.on_capture_control == nullptr) {
      throw std::runtime_error("capture control callback is not configured");
    }
    const char* result =
        callbacks_.on_capture_control(session_key.c_str(), enabled, user_data_);
    if (result == nullptr) {
      throw std::runtime_error("capture control callback returned null");
    }
    return result;
  }

  void Vibrate(const std::string& session_key, int duration_ms) override {
    if (callbacks_.on_vibrate == nullptr) {
      return;
    }
    callbacks_.on_vibrate(session_key.c_str(), duration_ms, user_data_);
  }

  std::string WebSearch(const std::string& session_key,
                        const std::string& query, int count,
                        const std::string& freshness) override {
    if (callbacks_.on_web_search == nullptr) {
      throw std::runtime_error("web_search callback is not configured");
    }
    const char* result =
        callbacks_.on_web_search(session_key.c_str(), query.c_str(), count,
                                 freshness.c_str(), user_data_);
    if (result == nullptr) {
      throw std::runtime_error("web_search callback returned null");
    }
    return result;
  }

  std::string WebFetch(const std::string& session_key, const std::string& url,
                       int max_chars) override {
    if (callbacks_.on_web_fetch == nullptr) {
      throw std::runtime_error("web_fetch callback is not configured");
    }
    const char* result = callbacks_.on_web_fetch(
        session_key.c_str(), url.c_str(), max_chars, user_data_);
    if (result == nullptr) {
      throw std::runtime_error("web_fetch callback returned null");
    }
    return result;
  }

  void SetForeground(bool foreground) {
    foreground_ = foreground;
  }

 private:
  std::string state_dir_;
  std::string models_dir_;
  ravbot_mobile_device_callbacks_t callbacks_{};
  void* user_data_ = nullptr;
  bool foreground_ = true;
};
}  // namespace ravbot::mobile

struct ravbot_mobile_engine {
  std::unique_ptr<ravbot::mobile::MobileEngine> impl;
  std::shared_ptr<ravbot::mobile::CallbackDeviceBridge> device_bridge;
  std::string state_dir;
  std::string models_dir;
  std::unordered_map<uint64_t, std::string> subscriptions;
  uint64_t next_id = 1;
};

namespace {

ravbot::RavBotConfig parse_config_or_default(const char* config_json) {
  if (!config_json || std::string(config_json).empty()) {
    return ravbot::RavBotConfig{};
  }
  auto parsed = nlohmann::json::parse(config_json);
  return ravbot::RavBotConfig::FromJson(parsed);
}

std::shared_ptr<spdlog::logger> create_logger(const char* log_level) {
  auto logger = spdlog::default_logger();
  if (log_level && *log_level) {
    logger->set_level(spdlog::level::from_str(log_level));
  }
  return logger;
}

int64_t now_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

extern "C" {

ravbot_mobile_engine_t* ravbot_mobile_init_engine(const char* config_json,
                                                  const char* state_dir,
                                                  const char* models_dir,
                                                  const char* log_level) {
  try {
    auto config = parse_config_or_default(config_json);
    auto logger = create_logger(log_level);
    auto state_path =
        std::filesystem::path((state_dir && *state_dir) ? state_dir : ".");
    auto models_path = std::filesystem::path(
        (models_dir && *models_dir) ? models_dir : state_path / "models");

    auto* engine = new ravbot_mobile_engine;
    engine->state_dir = state_path.string();
    engine->models_dir = models_path.string();
    engine->impl = std::make_unique<ravbot::mobile::MobileEngine>(
        std::move(config), state_path, models_path, logger);
    return engine;
  } catch (...) {
    return nullptr;
  }
}

void ravbot_mobile_free_engine(ravbot_mobile_engine_t* engine) {
  delete engine;
}

bool ravbot_mobile_start_session(ravbot_mobile_engine_t* engine,
                                 const char* session_key,
                                 const char* display_name) {
  if (!engine || !engine->impl || !session_key) {
    return false;
  }
  engine->impl->StartSession(session_key, display_name ? display_name : "");
  return true;
}

bool ravbot_mobile_send_text_turn(ravbot_mobile_engine_t* engine,
                                  const char* session_key, const char* text) {
  if (!engine || !engine->impl || !session_key || !text) {
    return false;
  }
  return engine->impl->SendTextTurn(session_key, text);
}

bool ravbot_mobile_push_pcm16(ravbot_mobile_engine_t* engine,
                              const char* session_key, const int16_t* samples,
                              size_t sample_count, int sample_rate_hz,
                              bool end_of_turn) {
  if (!engine || !engine->impl || !session_key) {
    return false;
  }
  return engine->impl->PushPcm16(session_key, samples, sample_count,
                                 sample_rate_hz, end_of_turn);
}

bool ravbot_mobile_flush_audio_turn(ravbot_mobile_engine_t* engine,
                                    const char* session_key) {
  if (!engine || !engine->impl || !session_key) {
    return false;
  }
  return engine->impl->FlushAudioTurn(session_key);
}

bool ravbot_mobile_push_camera_frame(ravbot_mobile_engine_t* engine,
                                     const char* session_key,
                                     const uint8_t* data, size_t data_size,
                                     int width, int height, const char* format,
                                     int64_t timestamp_ms) {
  if (!engine || !engine->impl || !session_key || !data || data_size == 0) {
    return false;
  }

  ravbot::mobile::CameraFrame frame;
  frame.width = width;
  frame.height = height;
  frame.format = format ? format : "rgba8888";
  frame.timestamp_ms = timestamp_ms;
  frame.data.assign(data, data + data_size);
  return engine->impl->PushCameraFrame(session_key, frame);
}

void ravbot_mobile_interrupt_generation(ravbot_mobile_engine_t* engine,
                                        const char* session_key) {
  if (!engine || !engine->impl || !session_key) {
    return;
  }
  engine->impl->InterruptGeneration(session_key);
}

bool ravbot_mobile_report_tts_state(ravbot_mobile_engine_t* engine,
                                    const char* session_key,
                                    const char* state) {
  if (!engine || !engine->impl || !session_key || !state) {
    return false;
  }
  return engine->impl->ReportTtsPlaybackState(session_key, state);
}

bool ravbot_mobile_report_device_status(
    ravbot_mobile_engine_t* engine, const char* session_key,
    bool service_running, bool capture_requested, bool permissions_granted,
    bool host_web_search_enabled, bool host_web_fetch_enabled,
    bool host_haptics_enabled, const char* microphone_status,
    const char* camera_status, const char* speaker_status) {
  if (!engine || !engine->impl || !session_key) {
    return false;
  }

  ravbot::mobile::DeviceStatusSnapshot status;
  status.service_running = service_running;
  status.capture_requested = capture_requested;
  status.permissions_granted = permissions_granted;
  status.host_web_search_enabled = host_web_search_enabled;
  status.host_web_fetch_enabled = host_web_fetch_enabled;
  status.host_haptics_enabled = host_haptics_enabled;
  status.microphone_status =
      microphone_status != nullptr ? microphone_status : "stopped";
  status.camera_status = camera_status != nullptr ? camera_status : "stopped";
  status.speaker_status = speaker_status != nullptr ? speaker_status : "idle";
  status.timestamp_ms = now_millis();
  return engine->impl->ReportDeviceStatus(session_key, std::move(status));
}

void ravbot_mobile_set_foreground_state(ravbot_mobile_engine_t* engine,
                                        bool foreground) {
  if (!engine || !engine->impl) {
    return;
  }
  engine->impl->SetForegroundState(foreground);
  if (engine->device_bridge) {
    engine->device_bridge->SetForeground(foreground);
  }
}

bool ravbot_mobile_set_device_callbacks(
    ravbot_mobile_engine_t* engine,
    const ravbot_mobile_device_callbacks_t* callbacks, void* user_data) {
  if (!engine || !engine->impl) {
    return false;
  }

  if (callbacks == nullptr) {
    engine->device_bridge.reset();
    engine->impl->SetDeviceBridge(nullptr);
    return true;
  }

  engine->device_bridge =
      std::make_shared<ravbot::mobile::CallbackDeviceBridge>(
          engine->state_dir, engine->models_dir, *callbacks, user_data);
  engine->impl->SetDeviceBridge(engine->device_bridge);
  return true;
}

uint64_t ravbot_mobile_subscribe_events(ravbot_mobile_engine_t* engine,
                                        ravbot_mobile_event_callback callback,
                                        void* user_data) {
  if (!engine || !engine->impl || !callback) {
    return 0;
  }

  uint64_t api_id = engine->next_id++;
  std::string internal_id = engine->impl->SubscribeEvents(
      [callback, user_data](const ravbot::mobile::MobileEvent& event) {
        std::string payload = event.payload.dump();
        callback(event.name.c_str(), payload.c_str(), user_data);
      });
  engine->subscriptions[api_id] = std::move(internal_id);
  return api_id;
}

void ravbot_mobile_unsubscribe_events(ravbot_mobile_engine_t* engine,
                                      uint64_t subscription_id) {
  if (!engine || !engine->impl) {
    return;
  }
  auto it = engine->subscriptions.find(subscription_id);
  if (it == engine->subscriptions.end()) {
    return;
  }
  engine->impl->UnsubscribeEvents(it->second);
  engine->subscriptions.erase(it);
}

}  // extern "C"
