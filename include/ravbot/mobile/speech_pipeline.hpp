// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "ravbot/config.hpp"
#include "ravbot/mobile/mobile_engine.hpp"

namespace ravbot::mobile {

class SpeechPipeline : public MobileAsrProvider {
 public:
  struct RuntimeStatus {
    bool backend_linked = false;
    bool asr_ready = false;
    bool tts_ready = false;
    bool stt_model_exists = false;
    bool tts_voice_exists = false;
    std::string stt_model;
    std::string tts_voice;
    std::string stt_model_path;
    std::string tts_voice_path;
    std::string detail;

    nlohmann::json ToJson() const;
  };

  struct SessionStatus {
    bool available = false;
    bool pending_audio = false;
    bool interrupted = false;
    size_t buffered_samples = 0;
    size_t buffered_chunks = 0;
    int sample_rate_hz = 16000;
    int duration_ms = 0;
    double average_level = 0.0;
    double peak_level = 0.0;
    uint64_t completed_segments = 0;
    uint64_t current_segment_index = 0;
    int64_t last_update_ms = 0;
    std::string state = "idle";
    std::string finalize_reason = "flush";
    std::string detail;

    nlohmann::json ToJson() const;
  };

  SpeechPipeline(const MobileModelsConfig& config,
                 const std::filesystem::path& models_dir,
                 std::shared_ptr<spdlog::logger> logger);

  AsrUpdate PushPcm16(const std::string& session_key, const int16_t* samples,
                      size_t sample_count, int sample_rate_hz,
                      bool end_of_turn) override;
  AsrUpdate Flush(const std::string& session_key) override;
  void Interrupt(const std::string& session_key) override;
  void ResetSession(const std::string& session_key) override;

  const RuntimeStatus& runtime_status() const {
    return runtime_status_;
  }
  SessionStatus GetSessionStatus(const std::string& session_key) const;

 private:
  struct SessionState {
    bool interrupted = false;
    bool has_pending_audio = false;
    size_t buffered_samples = 0;
    size_t buffered_chunks = 0;
    int sample_rate_hz = 16000;
    double accumulated_level = 0.0;
    double peak_level = 0.0;
    uint64_t utterance_index = 0;
    int64_t last_update_ms = 0;
    std::string finalize_reason = "flush";
  };

  RuntimeStatus BuildRuntimeStatus() const;
  SessionStatus BuildSessionStatus(const SessionState* state) const;
  AsrUpdate BuildPlaceholderUpdate(const SessionState& state,
                                   bool is_final) const;
  void ResetPendingUtterance(SessionState* state, bool complete_turn);

  MobileModelsConfig config_;
  std::filesystem::path models_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  RuntimeStatus runtime_status_;
  mutable std::mutex session_mutex_;
  std::unordered_map<std::string, SessionState> session_states_;
};

}  // namespace ravbot::mobile
