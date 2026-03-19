// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ravbot_mobile_engine ravbot_mobile_engine_t;

typedef void (*ravbot_mobile_event_callback)(
    const char* event_name,
    const char* payload_json,
    void* user_data);
typedef void (*ravbot_mobile_avatar_state_callback)(
    const char* state,
    void* user_data);
typedef void (*ravbot_mobile_speech_request_callback)(
    const char* text,
    void* user_data);
typedef void (*ravbot_mobile_speech_interrupt_callback)(void* user_data);
typedef void (*ravbot_mobile_vibrate_callback)(int duration_ms, void* user_data);
typedef const char* (*ravbot_mobile_web_search_callback)(
    const char* query,
    int count,
    const char* freshness,
    void* user_data);
typedef const char* (*ravbot_mobile_web_fetch_callback)(
    const char* url,
    int max_chars,
    void* user_data);

typedef struct ravbot_mobile_device_callbacks {
  ravbot_mobile_avatar_state_callback on_avatar_state;
  ravbot_mobile_speech_request_callback on_speech_request;
  ravbot_mobile_speech_interrupt_callback on_speech_interrupt;
  ravbot_mobile_vibrate_callback on_vibrate;
  ravbot_mobile_web_search_callback on_web_search;
  ravbot_mobile_web_fetch_callback on_web_fetch;
} ravbot_mobile_device_callbacks_t;

ravbot_mobile_engine_t* ravbot_mobile_init_engine(
    const char* config_json,
    const char* state_dir,
    const char* models_dir,
    const char* log_level);
void ravbot_mobile_free_engine(ravbot_mobile_engine_t* engine);

bool ravbot_mobile_start_session(ravbot_mobile_engine_t* engine,
                                 const char* session_key,
                                 const char* display_name);
bool ravbot_mobile_send_text_turn(ravbot_mobile_engine_t* engine,
                                  const char* session_key,
                                  const char* text);
bool ravbot_mobile_push_pcm16(ravbot_mobile_engine_t* engine,
                              const char* session_key,
                              const int16_t* samples,
                              size_t sample_count,
                              int sample_rate_hz,
                              bool end_of_turn);
bool ravbot_mobile_flush_audio_turn(ravbot_mobile_engine_t* engine,
                                    const char* session_key);
bool ravbot_mobile_push_camera_frame(ravbot_mobile_engine_t* engine,
                                     const char* session_key,
                                     const uint8_t* data,
                                     size_t data_size,
                                     int width,
                                     int height,
                                     const char* format,
                                     int64_t timestamp_ms);
void ravbot_mobile_interrupt_generation(ravbot_mobile_engine_t* engine,
                                        const char* session_key);
bool ravbot_mobile_report_tts_state(ravbot_mobile_engine_t* engine,
                                    const char* session_key,
                                    const char* state);
bool ravbot_mobile_report_device_status(ravbot_mobile_engine_t* engine,
                                        bool service_running,
                                        bool capture_requested,
                                        bool permissions_granted,
                                        bool host_web_search_enabled,
                                        bool host_web_fetch_enabled,
                                        const char* microphone_status,
                                        const char* camera_status,
                                        const char* speaker_status);
void ravbot_mobile_set_foreground_state(ravbot_mobile_engine_t* engine,
                                        bool foreground);
bool ravbot_mobile_set_device_callbacks(
    ravbot_mobile_engine_t* engine,
    const ravbot_mobile_device_callbacks_t* callbacks,
    void* user_data);
uint64_t ravbot_mobile_subscribe_events(ravbot_mobile_engine_t* engine,
                                        ravbot_mobile_event_callback callback,
                                        void* user_data);
void ravbot_mobile_unsubscribe_events(ravbot_mobile_engine_t* engine,
                                      uint64_t subscription_id);

#ifdef __cplusplus
}
#endif
