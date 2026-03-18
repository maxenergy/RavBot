// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>
#include <string>

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

  SpeechPipeline(const MobileModelsConfig& config,
                 const std::filesystem::path& models_dir,
                 std::shared_ptr<spdlog::logger> logger);

  AsrUpdate PushPcm16(const int16_t* samples,
                      size_t sample_count,
                      int sample_rate_hz,
                      bool end_of_turn) override;
  AsrUpdate Flush() override;
  void Interrupt() override;

  const RuntimeStatus& runtime_status() const { return runtime_status_; }

 private:
  RuntimeStatus BuildRuntimeStatus() const;
  AsrUpdate BuildPlaceholderUpdate(bool is_final) const;
  void ResetPendingUtterance(bool complete_turn);

  MobileModelsConfig config_;
  std::filesystem::path models_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  RuntimeStatus runtime_status_;
  bool interrupted_ = false;
  bool has_pending_audio_ = false;
  size_t buffered_samples_ = 0;
  size_t buffered_chunks_ = 0;
  int sample_rate_hz_ = 16000;
  double accumulated_level_ = 0.0;
  double peak_level_ = 0.0;
  uint64_t utterance_index_ = 0;
  std::string finalize_reason_ = "flush";
};

}  // namespace ravbot::mobile
