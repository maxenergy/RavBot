// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ravbot/mobile/mobile_c_api.h"

#include <gtest/gtest.h>

namespace {

bool json_array_contains_string(const nlohmann::json& array,
                                const std::string& value) {
  if (!array.is_array()) {
    return false;
  }
  return std::any_of(
      array.begin(), array.end(), [&value](const nlohmann::json& entry) {
        return entry.is_string() && entry.get<std::string>() == value;
      });
}

struct CapturedEvent {
  std::string name;
  nlohmann::json payload;
};

struct EventSink {
  std::mutex mutex;
  std::vector<CapturedEvent> events;
  std::vector<std::string> avatar_states;
  std::vector<std::string> speech_requests;
  std::vector<std::string> speech_interrupt_sessions;
  std::vector<std::string> vibration_requests;
  std::vector<std::string> web_search_requests;
  std::vector<std::string> web_fetch_requests;
};

void capture_event(const char* event_name, const char* payload_json,
                   void* user_data) {
  auto* sink = static_cast<EventSink*>(user_data);
  ASSERT_NE(sink, nullptr);

  nlohmann::json payload = nlohmann::json::object();
  if (payload_json != nullptr && *payload_json != '\0') {
    payload = nlohmann::json::parse(payload_json, nullptr, false);
    if (payload.is_discarded()) {
      payload = nlohmann::json::object();
    }
  }

  std::lock_guard<std::mutex> lock(sink->mutex);
  sink->events.push_back(
      {event_name != nullptr ? event_name : "", std::move(payload)});
}

void capture_avatar_state(const char* session_key, const char* state,
                          void* user_data) {
  auto* sink = static_cast<EventSink*>(user_data);
  ASSERT_NE(sink, nullptr);
  std::lock_guard<std::mutex> lock(sink->mutex);
  sink->avatar_states.push_back(
      std::string(session_key != nullptr ? session_key : "") + ":" +
      (state != nullptr ? state : ""));
}

void capture_speech_request(const char* session_key, const char* text,
                            void* user_data) {
  auto* sink = static_cast<EventSink*>(user_data);
  ASSERT_NE(sink, nullptr);
  std::lock_guard<std::mutex> lock(sink->mutex);
  sink->speech_requests.push_back(
      std::string(session_key != nullptr ? session_key : "") + ":" +
      (text != nullptr ? text : ""));
}

void capture_speech_interrupt(const char* session_key, void* user_data) {
  auto* sink = static_cast<EventSink*>(user_data);
  ASSERT_NE(sink, nullptr);
  std::lock_guard<std::mutex> lock(sink->mutex);
  sink->speech_interrupt_sessions.push_back(session_key != nullptr ? session_key
                                                                   : "");
}

void capture_vibrate(const char* session_key, int duration_ms,
                     void* user_data) {
  auto* sink = static_cast<EventSink*>(user_data);
  ASSERT_NE(sink, nullptr);
  std::lock_guard<std::mutex> lock(sink->mutex);
  sink->vibration_requests.push_back(
      std::string(session_key != nullptr ? session_key : "") + ":" +
      std::to_string(duration_ms));
}

const char* capture_web_search(const char* session_key, const char* query,
                               int /*count*/, const char* /*freshness*/,
                               void* user_data) {
  auto* sink = static_cast<EventSink*>(user_data);
  if (sink == nullptr) {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(sink->mutex);
  sink->web_search_requests.push_back(
      std::string(session_key != nullptr ? session_key : "") + ":" +
      (query != nullptr ? query : ""));
  return R"({"sessionKey":"agent:main:web-search-only","results":[{"title":"RavBot","url":"https://example.com"}]})";
}

const char* capture_web_fetch(const char* session_key, const char* url,
                              int /*max_chars*/, void* user_data) {
  auto* sink = static_cast<EventSink*>(user_data);
  if (sink == nullptr) {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(sink->mutex);
  sink->web_fetch_requests.push_back(
      std::string(session_key != nullptr ? session_key : "") + ":" +
      (url != nullptr ? url : ""));
  return R"({"sessionKey":"agent:main:web-fetch-only","content":"RavBot Android page"})";
}

class MobileCApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = std::filesystem::temp_directory_path() / "ravbot_mobile_c_api";
    std::filesystem::remove_all(test_dir_);
    std::filesystem::create_directories(test_dir_);
  }

  void TearDown() override {
    std::filesystem::remove_all(test_dir_);
  }

  std::string MakeConfigJson() const {
    nlohmann::json config = {
        {"mobile",
         {{"runtime", {{"enabled", true}, {"mode", "local"}}},
          {"vision", {{"enabled", true}}}}},
    };
    return config.dump();
  }

  std::filesystem::path test_dir_;
};

TEST_F(MobileCApiTest, InitEngineRejectsInvalidJson) {
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      "{", test_dir_.c_str(), test_dir_.c_str(), "info");
  EXPECT_EQ(engine, nullptr);
}

TEST_F(MobileCApiTest, SendTextTurnEmitsAssistantFinalEvent) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  uint64_t subscription_id =
      ravbot_mobile_subscribe_events(engine, capture_event, &sink);
  ASSERT_NE(subscription_id, 0u);

  EXPECT_TRUE(
      ravbot_mobile_start_session(engine, "agent:main:capi", "Android C API"));
  EXPECT_TRUE(
      ravbot_mobile_send_text_turn(engine, "agent:main:capi", "hello ravbot"));

  ravbot_mobile_unsubscribe_events(engine, subscription_id);
  ravbot_mobile_free_engine(engine);

  bool saw_avatar = false;
  bool saw_final = false;
  bool saw_runtime = false;
  for (const auto& event : sink.events) {
    if (event.name == "mobile.avatar_state") {
      saw_avatar = true;
    }
    if (event.name == "mobile.runtime_status") {
      saw_runtime = true;
      EXPECT_EQ(event.payload["modelsDir"], test_dir_.string());
      EXPECT_EQ(event.payload["speechProvider"], "sherpa_onnx_mobile");
      EXPECT_EQ(event.payload["visionProvider"], "placeholder_mobile_vision");
      EXPECT_TRUE(event.payload["visionProviderPlaceholder"].get<bool>());
      EXPECT_EQ(
          event.payload["visionDetail"],
          "Using placeholder mobile vision provider until a real VLM backend "
          "is linked.");
      EXPECT_FALSE(event.payload["deviceBridgeAttached"].get<bool>());
      EXPECT_FALSE(event.payload["webSearchReady"].get<bool>());
      EXPECT_FALSE(event.payload["webFetchReady"].get<bool>());
      EXPECT_FALSE(event.payload["vibrationReady"].get<bool>());
      EXPECT_TRUE(json_array_contains_string(event.payload["availableTools"],
                                             "device_status"));
      EXPECT_TRUE(json_array_contains_string(event.payload["availableTools"],
                                             "runtime_status"));
      EXPECT_FALSE(json_array_contains_string(event.payload["availableTools"],
                                              "web_search"));
      EXPECT_EQ(event.payload["toolAvailability"]["web_search"]["reason"],
                "device_bridge_missing");
      EXPECT_TRUE(event.payload.contains("detail"));
      EXPECT_TRUE(event.payload.contains("speechDetail"));
    }
    if (event.name == "assistant_final") {
      saw_final = true;
      EXPECT_EQ(event.payload["sessionKey"], "agent:main:capi");
      EXPECT_TRUE(event.payload.contains("text"));
      EXPECT_FALSE(event.payload["text"].get<std::string>().empty());
    }
  }

  EXPECT_TRUE(saw_avatar);
  EXPECT_TRUE(saw_runtime);
  EXPECT_TRUE(saw_final);
}

TEST_F(MobileCApiTest, UnsubscribeStopsFutureCallbacks) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  uint64_t subscription_id =
      ravbot_mobile_subscribe_events(engine, capture_event, &sink);
  ASSERT_NE(subscription_id, 0u);

  EXPECT_TRUE(ravbot_mobile_start_session(engine, "agent:main:unsubscribe",
                                          "Before unsubscribe"));
  ravbot_mobile_unsubscribe_events(engine, subscription_id);

  const size_t event_count_after_unsubscribe = sink.events.size();
  EXPECT_TRUE(ravbot_mobile_send_text_turn(engine, "agent:main:unsubscribe",
                                           "should not notify"));
  EXPECT_EQ(sink.events.size(), event_count_after_unsubscribe);

  ravbot_mobile_free_engine(engine);
}

TEST_F(MobileCApiTest, CameraFrameEmitsVisionObservationByDefault) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  uint64_t subscription_id =
      ravbot_mobile_subscribe_events(engine, capture_event, &sink);
  ASSERT_NE(subscription_id, 0u);

  EXPECT_TRUE(
      ravbot_mobile_start_session(engine, "agent:main:camera", "Camera path"));

  const uint8_t frame_bytes[] = {0, 1, 2, 3};
  EXPECT_TRUE(ravbot_mobile_push_camera_frame(engine, "agent:main:camera",
                                              frame_bytes, sizeof(frame_bytes),
                                              2, 2, "rgba8888", 42));

  ravbot_mobile_unsubscribe_events(engine, subscription_id);
  ravbot_mobile_free_engine(engine);

  bool saw_vision = false;
  for (const auto& event : sink.events) {
    if (event.name == "mobile.vision_observation") {
      saw_vision = true;
      EXPECT_EQ(event.payload["sessionKey"], "agent:main:camera");
      EXPECT_TRUE(event.payload.contains("summary"));
    }
  }
  EXPECT_TRUE(saw_vision);
}

TEST_F(MobileCApiTest, ReportTtsStateEmitsPlaybackUpdate) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  uint64_t subscription_id =
      ravbot_mobile_subscribe_events(engine, capture_event, &sink);
  ASSERT_NE(subscription_id, 0u);

  EXPECT_TRUE(
      ravbot_mobile_start_session(engine, "agent:main:tts", "Playback state"));
  EXPECT_TRUE(
      ravbot_mobile_report_tts_state(engine, "agent:main:tts", "speaking"));

  ravbot_mobile_unsubscribe_events(engine, subscription_id);
  ravbot_mobile_free_engine(engine);

  bool saw_tts = false;
  for (const auto& event : sink.events) {
    if (event.name == "mobile.tts_state" &&
        event.payload.value("state", "") == "speaking") {
      saw_tts = true;
      EXPECT_EQ(event.payload["sessionKey"], "agent:main:tts");
    }
  }
  EXPECT_TRUE(saw_tts);
}

TEST_F(MobileCApiTest, ReportDeviceStatusEmitsNativeSnapshot) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  uint64_t subscription_id =
      ravbot_mobile_subscribe_events(engine, capture_event, &sink);
  ASSERT_NE(subscription_id, 0u);

  EXPECT_TRUE(ravbot_mobile_report_device_status(
      engine, "agent:main:device-status", true, true, true, false, true, false,
      "running", "running", "speaking"));

  ravbot_mobile_unsubscribe_events(engine, subscription_id);
  ravbot_mobile_free_engine(engine);

  bool saw_device_status = false;
  for (const auto& event : sink.events) {
    if (event.name == "mobile.device_status") {
      saw_device_status = true;
      EXPECT_EQ(event.payload["sessionKey"], "agent:main:device-status");
      EXPECT_TRUE(event.payload["foreground"]);
      EXPECT_TRUE(event.payload["serviceRunning"]);
      EXPECT_TRUE(event.payload["captureRequested"]);
      EXPECT_TRUE(event.payload["permissionsGranted"]);
      EXPECT_FALSE(event.payload["hostWebSearchEnabled"]);
      EXPECT_TRUE(event.payload["hostWebFetchEnabled"]);
      EXPECT_FALSE(event.payload["hostHapticsEnabled"]);
      EXPECT_EQ(event.payload["microphoneStatus"], "running");
      EXPECT_EQ(event.payload["cameraStatus"], "running");
      EXPECT_EQ(event.payload["speakerStatus"], "speaking");
      EXPECT_TRUE(event.payload.contains("hostCapabilities"));
      EXPECT_TRUE(event.payload["hostCapabilities"]["capture"]["ready"]);
      EXPECT_EQ(event.payload["hostCapabilities"]["webSearch"]["reason"],
                "device_bridge_missing");
      EXPECT_EQ(event.payload["hostCapabilities"]["haptics"]["reason"],
                "device_bridge_missing");
      EXPECT_TRUE(event.payload.contains("speechState"));
      EXPECT_FALSE(event.payload["speechState"]["available"]);
      EXPECT_EQ(event.payload["speechState"]["state"], "idle");
    }
  }

  EXPECT_TRUE(saw_device_status);
}

TEST_F(MobileCApiTest, FlushAudioTurnPromotesBufferedSpeechToFinalTurn) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  uint64_t subscription_id =
      ravbot_mobile_subscribe_events(engine, capture_event, &sink);
  ASSERT_NE(subscription_id, 0u);

  EXPECT_TRUE(ravbot_mobile_start_session(engine, "agent:main:flush",
                                          "Buffered speech"));
  int16_t samples[] = {1, 2, 3, 4};
  EXPECT_TRUE(ravbot_mobile_push_pcm16(engine, "agent:main:flush", samples, 4,
                                       16000, false));
  EXPECT_TRUE(ravbot_mobile_flush_audio_turn(engine, "agent:main:flush"));

  ravbot_mobile_unsubscribe_events(engine, subscription_id);
  ravbot_mobile_free_engine(engine);

  bool saw_partial = false;
  bool saw_final = false;
  for (const auto& event : sink.events) {
    if (event.name == "mobile.asr_partial") {
      saw_partial = true;
      EXPECT_EQ(event.payload["sessionKey"], "agent:main:flush");
      EXPECT_EQ(event.payload["segmentIndex"], 1);
      EXPECT_EQ(event.payload["endReason"], "streaming");
    }
    if (event.name == "mobile.asr_final") {
      saw_final = true;
      EXPECT_EQ(event.payload["sessionKey"], "agent:main:flush");
      EXPECT_NE(event.payload.value("text", "").find("audio segment 1"),
                std::string::npos);
      EXPECT_EQ(event.payload["segmentIndex"], 1);
      EXPECT_EQ(event.payload["sampleRateHz"], 16000);
      EXPECT_EQ(event.payload["endReason"], "flush");
    }
  }

  EXPECT_TRUE(saw_partial);
  EXPECT_TRUE(saw_final);
}

TEST_F(MobileCApiTest, DeviceCallbacksReceiveAvatarAndSpeechRequests) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  ravbot_mobile_device_callbacks_t callbacks{};
  callbacks.on_avatar_state = capture_avatar_state;
  callbacks.on_speech_request = capture_speech_request;
  callbacks.on_speech_interrupt = capture_speech_interrupt;
  ASSERT_TRUE(ravbot_mobile_set_device_callbacks(engine, &callbacks, &sink));

  EXPECT_TRUE(ravbot_mobile_start_session(engine, "agent:main:device",
                                          "Device bridge"));
  EXPECT_TRUE(ravbot_mobile_send_text_turn(engine, "agent:main:device",
                                           "hello device"));
  ravbot_mobile_interrupt_generation(engine, "agent:main:device");

  ravbot_mobile_free_engine(engine);

  EXPECT_FALSE(sink.avatar_states.empty());
  EXPECT_FALSE(sink.speech_requests.empty());
  EXPECT_EQ(sink.avatar_states.front(), "agent:main:device:idle");
  EXPECT_FALSE(sink.speech_interrupt_sessions.empty());
  EXPECT_EQ(sink.speech_requests.front().rfind("agent:main:device:", 0), 0u);
  EXPECT_GT(sink.speech_requests.front().size(),
            std::string("agent:main:device:").size());
  EXPECT_EQ(sink.speech_interrupt_sessions.front(), "agent:main:device");
}

TEST_F(MobileCApiTest, RuntimeStatusReflectsMissingWebDeviceCallbacks) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  ravbot_mobile_device_callbacks_t callbacks{};
  callbacks.on_avatar_state = capture_avatar_state;
  callbacks.on_speech_request = capture_speech_request;
  callbacks.on_speech_interrupt = capture_speech_interrupt;
  ASSERT_TRUE(ravbot_mobile_set_device_callbacks(engine, &callbacks, &sink));

  uint64_t subscription_id =
      ravbot_mobile_subscribe_events(engine, capture_event, &sink);
  ASSERT_NE(subscription_id, 0u);

  EXPECT_TRUE(ravbot_mobile_start_session(engine, "agent:main:device-runtime",
                                          "Device runtime status"));

  ravbot_mobile_unsubscribe_events(engine, subscription_id);
  ravbot_mobile_free_engine(engine);

  bool saw_runtime = false;
  for (const auto& event : sink.events) {
    if (event.name != "mobile.runtime_status") {
      continue;
    }
    saw_runtime = true;
    EXPECT_TRUE(event.payload["deviceBridgeAttached"].get<bool>());
    EXPECT_FALSE(event.payload["webSearchReady"].get<bool>());
    EXPECT_FALSE(event.payload["webFetchReady"].get<bool>());
    EXPECT_FALSE(event.payload["vibrationReady"].get<bool>());
  }

  EXPECT_TRUE(saw_runtime);
}

TEST_F(MobileCApiTest, RuntimeStatusReflectsPartialWebDeviceCallbacks) {
  std::string config_json = MakeConfigJson();

  {
    ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
        config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
    ASSERT_NE(engine, nullptr);

    EventSink sink;
    ravbot_mobile_device_callbacks_t callbacks{};
    callbacks.on_web_search = capture_web_search;
    ASSERT_TRUE(ravbot_mobile_set_device_callbacks(engine, &callbacks, &sink));

    uint64_t subscription_id =
        ravbot_mobile_subscribe_events(engine, capture_event, &sink);
    ASSERT_NE(subscription_id, 0u);
    EXPECT_TRUE(ravbot_mobile_start_session(
        engine, "agent:main:web-search-only", "Web search only"));

    ravbot_mobile_unsubscribe_events(engine, subscription_id);
    ravbot_mobile_free_engine(engine);

    bool saw_runtime = false;
    for (const auto& event : sink.events) {
      if (event.name != "mobile.runtime_status") {
        continue;
      }
      saw_runtime = true;
      EXPECT_TRUE(event.payload["deviceBridgeAttached"].get<bool>());
      EXPECT_TRUE(event.payload["webSearchReady"].get<bool>());
      EXPECT_FALSE(event.payload["webFetchReady"].get<bool>());
      EXPECT_FALSE(event.payload["vibrationReady"].get<bool>());
      EXPECT_TRUE(json_array_contains_string(event.payload["availableTools"],
                                             "web_search"));
      EXPECT_FALSE(json_array_contains_string(event.payload["availableTools"],
                                              "web_fetch"));
      EXPECT_EQ(event.payload["toolAvailability"]["web_fetch"]["reason"],
                "web_fetch_unsupported");
    }
    EXPECT_TRUE(saw_runtime);
  }

  {
    ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
        config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
    ASSERT_NE(engine, nullptr);

    EventSink sink;
    ravbot_mobile_device_callbacks_t callbacks{};
    callbacks.on_web_fetch = capture_web_fetch;
    ASSERT_TRUE(ravbot_mobile_set_device_callbacks(engine, &callbacks, &sink));

    uint64_t subscription_id =
        ravbot_mobile_subscribe_events(engine, capture_event, &sink);
    ASSERT_NE(subscription_id, 0u);
    EXPECT_TRUE(ravbot_mobile_start_session(engine, "agent:main:web-fetch-only",
                                            "Web fetch only"));

    ravbot_mobile_unsubscribe_events(engine, subscription_id);
    ravbot_mobile_free_engine(engine);

    bool saw_runtime = false;
    for (const auto& event : sink.events) {
      if (event.name != "mobile.runtime_status") {
        continue;
      }
      saw_runtime = true;
      EXPECT_TRUE(event.payload["deviceBridgeAttached"].get<bool>());
      EXPECT_FALSE(event.payload["webSearchReady"].get<bool>());
      EXPECT_TRUE(event.payload["webFetchReady"].get<bool>());
      EXPECT_FALSE(event.payload["vibrationReady"].get<bool>());
      EXPECT_FALSE(json_array_contains_string(event.payload["availableTools"],
                                              "web_search"));
      EXPECT_TRUE(json_array_contains_string(event.payload["availableTools"],
                                             "web_fetch"));
      EXPECT_EQ(event.payload["toolAvailability"]["web_search"]["reason"],
                "web_search_unsupported");
    }
    EXPECT_TRUE(saw_runtime);
  }
}

TEST_F(MobileCApiTest, RuntimeStatusReflectsVibrationDeviceCallback) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  ravbot_mobile_device_callbacks_t callbacks{};
  callbacks.on_vibrate = capture_vibrate;
  ASSERT_TRUE(ravbot_mobile_set_device_callbacks(engine, &callbacks, &sink));

  uint64_t subscription_id =
      ravbot_mobile_subscribe_events(engine, capture_event, &sink);
  ASSERT_NE(subscription_id, 0u);
  EXPECT_TRUE(ravbot_mobile_start_session(engine, "agent:main:vibration-ready",
                                          "Vibration ready"));

  ravbot_mobile_unsubscribe_events(engine, subscription_id);
  ravbot_mobile_free_engine(engine);

  bool saw_runtime = false;
  for (const auto& event : sink.events) {
    if (event.name != "mobile.runtime_status") {
      continue;
    }
    saw_runtime = true;
    EXPECT_TRUE(event.payload["deviceBridgeAttached"].get<bool>());
    EXPECT_TRUE(event.payload["vibrationReady"].get<bool>());
    EXPECT_TRUE(
        json_array_contains_string(event.payload["availableTools"], "vibrate"));
    EXPECT_EQ(event.payload["toolAvailability"]["vibrate"]["available"], true);
  }
  EXPECT_TRUE(saw_runtime);
}

TEST_F(MobileCApiTest, SettingDeviceCallbacksEmitsRuntimeStatusUpdate) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  uint64_t subscription_id =
      ravbot_mobile_subscribe_events(engine, capture_event, &sink);
  ASSERT_NE(subscription_id, 0u);

  ravbot_mobile_device_callbacks_t callbacks{};
  callbacks.on_avatar_state = capture_avatar_state;
  callbacks.on_web_search = capture_web_search;
  ASSERT_TRUE(ravbot_mobile_set_device_callbacks(engine, &callbacks, &sink));

  ravbot_mobile_unsubscribe_events(engine, subscription_id);
  ravbot_mobile_free_engine(engine);

  std::vector<nlohmann::json> runtime_payloads;
  for (const auto& event : sink.events) {
    if (event.name == "mobile.runtime_status") {
      runtime_payloads.push_back(event.payload);
    }
  }

  ASSERT_GE(runtime_payloads.size(), 2u);
  EXPECT_FALSE(runtime_payloads[0]["deviceBridgeAttached"].get<bool>());
  EXPECT_FALSE(runtime_payloads[0]["webSearchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[0]["webFetchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[0]["vibrationReady"].get<bool>());

  EXPECT_TRUE(runtime_payloads[1]["deviceBridgeAttached"].get<bool>());
  EXPECT_TRUE(runtime_payloads[1]["webSearchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[1]["webFetchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[1]["vibrationReady"].get<bool>());
}

TEST_F(MobileCApiTest, ClearingDeviceCallbacksEmitsRuntimeStatusUpdate) {
  std::string config_json = MakeConfigJson();
  ravbot_mobile_engine_t* engine = ravbot_mobile_init_engine(
      config_json.c_str(), test_dir_.c_str(), test_dir_.c_str(), "info");
  ASSERT_NE(engine, nullptr);

  EventSink sink;
  uint64_t subscription_id =
      ravbot_mobile_subscribe_events(engine, capture_event, &sink);
  ASSERT_NE(subscription_id, 0u);

  ravbot_mobile_device_callbacks_t callbacks{};
  callbacks.on_avatar_state = capture_avatar_state;
  callbacks.on_web_search = capture_web_search;
  ASSERT_TRUE(ravbot_mobile_set_device_callbacks(engine, &callbacks, &sink));
  ASSERT_TRUE(ravbot_mobile_set_device_callbacks(engine, nullptr, nullptr));

  ravbot_mobile_unsubscribe_events(engine, subscription_id);
  ravbot_mobile_free_engine(engine);

  std::vector<nlohmann::json> runtime_payloads;
  for (const auto& event : sink.events) {
    if (event.name == "mobile.runtime_status") {
      runtime_payloads.push_back(event.payload);
    }
  }

  ASSERT_GE(runtime_payloads.size(), 3u);
  EXPECT_FALSE(runtime_payloads[0]["deviceBridgeAttached"].get<bool>());
  EXPECT_TRUE(runtime_payloads[1]["deviceBridgeAttached"].get<bool>());
  EXPECT_TRUE(runtime_payloads[1]["webSearchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[2]["deviceBridgeAttached"].get<bool>());
  EXPECT_FALSE(runtime_payloads[2]["webSearchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[2]["webFetchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[2]["vibrationReady"].get<bool>());
}

}  // namespace
