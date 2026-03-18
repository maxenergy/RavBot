// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/mobile/speech_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>

#include "ravbot/config.hpp"

namespace ravbot::mobile {

namespace {

constexpr bool kSherpaOnnxBackendLinked =
#ifdef RAVBOT_ENABLE_SHERPA_ONNX_MOBILE_BACKEND
    true;
#else
    false;
#endif
    ;

std::string ResolveAssetPath(const std::string& configured_path,
                             const std::filesystem::path& models_dir) {
  if (configured_path.empty()) {
    return {};
  }

  std::filesystem::path resolved(
      RavBotConfig::ExpandHome(configured_path));
  if (resolved.is_relative() && !models_dir.empty()) {
    resolved = models_dir / resolved;
  }
  return resolved.lexically_normal().string();
}

bool PathExists(const std::string& path) {
  return !path.empty() && std::filesystem::exists(std::filesystem::path(path));
}

std::string BuildSpeechDetail(const SpeechPipeline::RuntimeStatus& status) {
  if (status.stt_model_path.empty()) {
    return "No local sherpa-onnx STT model is configured for mobile runtime.";
  }
  if (!status.stt_model_exists) {
    return "Configured sherpa-onnx STT model is missing: " +
           status.stt_model_path;
  }
  if (!status.backend_linked) {
    return "sherpa-onnx mobile backend is not linked into this build.";
  }
  if (!status.tts_voice_path.empty() && !status.tts_voice_exists) {
    return "STT assets are present, but TTS voice assets are missing: " +
           status.tts_voice_path;
  }
  if (status.asr_ready && status.tts_ready) {
    return "sherpa-onnx speech runtime is ready for ASR and TTS.";
  }
  if (status.asr_ready) {
    return "sherpa-onnx speech runtime is ready for ASR.";
  }
  return "sherpa-onnx speech runtime is configured but not ready.";
}

double AverageAbsLevel(const int16_t* samples, size_t sample_count) {
  if (samples == nullptr || sample_count == 0) {
    return 0.0;
  }

  double total = 0.0;
  for (size_t i = 0; i < sample_count; ++i) {
    total += std::abs(static_cast<int>(samples[i]));
  }
  return total / static_cast<double>(sample_count);
}

double PeakAbsLevel(const int16_t* samples, size_t sample_count) {
  if (samples == nullptr || sample_count == 0) {
    return 0.0;
  }

  double peak = 0.0;
  for (size_t i = 0; i < sample_count; ++i) {
    peak = std::max(peak, static_cast<double>(std::abs(static_cast<int>(samples[i]))));
  }
  return peak;
}

}  // namespace

nlohmann::json SpeechPipeline::RuntimeStatus::ToJson() const {
  return {{"speechProvider", "sherpa_onnx_mobile"},
          {"speechBackend", "sherpa-onnx"},
          {"speechBackendLinked", backend_linked},
          {"asrReady", asr_ready},
          {"ttsReady", tts_ready},
          {"sttModel", stt_model},
          {"sttModelPath", stt_model_path},
          {"sttModelExists", stt_model_exists},
          {"ttsVoice", tts_voice},
          {"ttsVoicePath", tts_voice_path},
          {"ttsVoiceExists", tts_voice_exists},
          {"speechDetail", detail}};
}

SpeechPipeline::SpeechPipeline(const MobileModelsConfig& config,
                               const std::filesystem::path& models_dir,
                               std::shared_ptr<spdlog::logger> logger)
    : config_(config),
      models_dir_(models_dir),
      logger_(logger ? logger : spdlog::default_logger()),
      runtime_status_(BuildRuntimeStatus()) {
  if (logger_) {
    logger_->info("Configured mobile speech pipeline: {}",
                  runtime_status_.detail);
  }
}

AsrUpdate SpeechPipeline::PushPcm16(const int16_t* samples,
                                    size_t sample_count,
                                    int sample_rate_hz,
                                    bool end_of_turn) {
  if (samples == nullptr || sample_count == 0) {
    return {};
  }

  if (interrupted_) {
    interrupted_ = false;
    ResetPendingUtterance(false);
    return {};
  }

  sample_rate_hz_ = sample_rate_hz > 0 ? sample_rate_hz : sample_rate_hz_;
  buffered_samples_ += sample_count;
  buffered_chunks_ += 1;
  accumulated_level_ += AverageAbsLevel(samples, sample_count);
  peak_level_ = std::max(peak_level_, PeakAbsLevel(samples, sample_count));
  has_pending_audio_ = true;

  if (logger_) {
    logger_->debug("SpeechPipeline accepted {} samples (final={})",
                   sample_count, end_of_turn);
  }

  if (!end_of_turn) {
    if (buffered_chunks_ == 1 || buffered_chunks_ % 3 == 0) {
      return BuildPlaceholderUpdate(false);
    }
    return {};
  }

  finalize_reason_ = "vad_silence";
  return Flush();
}

AsrUpdate SpeechPipeline::Flush() {
  if (interrupted_) {
    interrupted_ = false;
    ResetPendingUtterance(false);
    return {};
  }
  if (!has_pending_audio_) {
    return {};
  }

  AsrUpdate update = BuildPlaceholderUpdate(true);
  ResetPendingUtterance(true);
  return update;
}

void SpeechPipeline::Interrupt() {
  interrupted_ = true;
  ResetPendingUtterance(false);
}

AsrUpdate SpeechPipeline::BuildPlaceholderUpdate(bool is_final) const {
  if (!has_pending_audio_) {
    return {};
  }

  const int sample_rate = sample_rate_hz_ > 0 ? sample_rate_hz_ : 16000;
  const double duration_seconds =
      static_cast<double>(buffered_samples_) / static_cast<double>(sample_rate);
  const int duration_ms = std::max(
      1, static_cast<int>(duration_seconds * 1000.0));
  const double average_level =
      buffered_chunks_ > 0
          ? accumulated_level_ / static_cast<double>(buffered_chunks_)
          : 0.0;
  const double average_norm = average_level / 32767.0;
  const double peak_norm = peak_level_ / 32767.0;

  std::ostringstream text;
  text.setf(std::ios::fixed);
  text.precision(2);

  if (!is_final) {
    text << "listening on device (" << duration_seconds << "s, avg "
         << average_norm << ")";
    AsrUpdate update;
    update.text = text.str();
    update.is_final = false;
    update.segment_index = utterance_index_ + 1;
    update.sample_rate_hz = sample_rate;
    update.duration_ms = duration_ms;
    update.average_level = average_norm;
    update.peak_level = peak_norm;
    update.end_reason = "streaming";
    return update;
  }

  const uint64_t segment_number = utterance_index_ + 1;
  text << (runtime_status_.asr_ready ? "speech segment "
                                     : "audio segment ")
       << segment_number
       << (runtime_status_.asr_ready ? " captured on device"
                                     : " received on device")
       << " (" << duration_seconds << "s, avg " << average_norm
       << ", peak " << peak_norm << ")";
  AsrUpdate update;
  update.text = text.str();
  update.is_final = true;
  update.segment_index = segment_number;
  update.sample_rate_hz = sample_rate;
  update.duration_ms = duration_ms;
  update.average_level = average_norm;
  update.peak_level = peak_norm;
  update.end_reason = finalize_reason_;
  return update;
}

void SpeechPipeline::ResetPendingUtterance(bool complete_turn) {
  if (complete_turn && has_pending_audio_) {
    utterance_index_ += 1;
  }
  has_pending_audio_ = false;
  buffered_samples_ = 0;
  buffered_chunks_ = 0;
  accumulated_level_ = 0.0;
  peak_level_ = 0.0;
  finalize_reason_ = "flush";
}

SpeechPipeline::RuntimeStatus SpeechPipeline::BuildRuntimeStatus() const {
  RuntimeStatus status;
  status.backend_linked = kSherpaOnnxBackendLinked;
  status.stt_model = config_.stt_model;
  status.tts_voice = config_.tts_voice;
  status.stt_model_path = ResolveAssetPath(config_.stt_model, models_dir_);
  status.tts_voice_path = ResolveAssetPath(config_.tts_voice, models_dir_);
  status.stt_model_exists = PathExists(status.stt_model_path);
  status.tts_voice_exists = PathExists(status.tts_voice_path);
  status.asr_ready = status.backend_linked && status.stt_model_exists;
  status.tts_ready = status.backend_linked && status.tts_voice_exists;
  status.detail = BuildSpeechDetail(status);
  return status;
}

}  // namespace ravbot::mobile
