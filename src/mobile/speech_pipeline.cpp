// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/mobile/speech_pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
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

  std::filesystem::path resolved(RavBotConfig::ExpandHome(configured_path));
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
    peak = std::max(
        peak, static_cast<double>(std::abs(static_cast<int>(samples[i]))));
  }
  return peak;
}

int64_t now_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

nlohmann::json SpeechPipeline::SessionStatus::ToJson() const {
  return {{"available", available},
          {"pendingAudio", pending_audio},
          {"interrupted", interrupted},
          {"bufferedSamples", buffered_samples},
          {"bufferedChunks", buffered_chunks},
          {"sampleRateHz", sample_rate_hz},
          {"durationMs", duration_ms},
          {"averageLevel", average_level},
          {"peakLevel", peak_level},
          {"completedSegments", completed_segments},
          {"currentSegmentIndex", current_segment_index},
          {"lastUpdateMs", last_update_ms},
          {"state", state},
          {"finalizeReason", finalize_reason},
          {"detail", detail}};
}

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

AsrUpdate SpeechPipeline::PushPcm16(const std::string& session_key,
                                    const int16_t* samples, size_t sample_count,
                                    int sample_rate_hz, bool end_of_turn) {
  if (samples == nullptr || sample_count == 0) {
    return {};
  }

  std::lock_guard<std::mutex> lock(session_mutex_);
  auto& state = session_states_[session_key];
  if (state.interrupted) {
    state.interrupted = false;
    ResetPendingUtterance(&state, false);
    state.last_update_ms = now_millis();
    return {};
  }

  state.sample_rate_hz =
      sample_rate_hz > 0 ? sample_rate_hz : state.sample_rate_hz;
  state.buffered_samples += sample_count;
  state.buffered_chunks += 1;
  state.accumulated_level += AverageAbsLevel(samples, sample_count);
  state.peak_level =
      std::max(state.peak_level, PeakAbsLevel(samples, sample_count));
  state.has_pending_audio = true;
  state.last_update_ms = now_millis();

  if (logger_) {
    logger_->debug(
        "SpeechPipeline accepted {} samples for session {} (final={})",
        sample_count, session_key, end_of_turn);
  }

  if (!end_of_turn) {
    if (state.buffered_chunks == 1 || state.buffered_chunks % 3 == 0) {
      return BuildPlaceholderUpdate(state, false);
    }
    return {};
  }

  state.finalize_reason = "vad_silence";
  AsrUpdate update = BuildPlaceholderUpdate(state, true);
  ResetPendingUtterance(&state, true);
  state.last_update_ms = now_millis();
  return update;
}

AsrUpdate SpeechPipeline::Flush(const std::string& session_key) {
  std::lock_guard<std::mutex> lock(session_mutex_);
  auto it = session_states_.find(session_key);
  if (it == session_states_.end()) {
    return {};
  }

  auto& state = it->second;
  if (state.interrupted) {
    state.interrupted = false;
    ResetPendingUtterance(&state, false);
    state.last_update_ms = now_millis();
    return {};
  }
  if (!state.has_pending_audio) {
    return {};
  }

  AsrUpdate update = BuildPlaceholderUpdate(state, true);
  ResetPendingUtterance(&state, true);
  state.last_update_ms = now_millis();
  return update;
}

void SpeechPipeline::Interrupt(const std::string& session_key) {
  std::lock_guard<std::mutex> lock(session_mutex_);
  auto& state = session_states_[session_key];
  state.interrupted = true;
  ResetPendingUtterance(&state, false);
  state.last_update_ms = now_millis();
}

void SpeechPipeline::ResetSession(const std::string& session_key) {
  if (session_key.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(session_mutex_);
  session_states_.erase(session_key);
}

SpeechPipeline::SessionStatus
SpeechPipeline::GetSessionStatus(const std::string& session_key) const {
  std::lock_guard<std::mutex> lock(session_mutex_);
  const auto it = session_states_.find(session_key);
  return BuildSessionStatus(it == session_states_.end() ? nullptr
                                                        : &it->second);
}

AsrUpdate SpeechPipeline::BuildPlaceholderUpdate(const SessionState& state,
                                                 bool is_final) const {
  if (!state.has_pending_audio) {
    return {};
  }

  const int sample_rate =
      state.sample_rate_hz > 0 ? state.sample_rate_hz : 16000;
  const double duration_seconds = static_cast<double>(state.buffered_samples) /
                                  static_cast<double>(sample_rate);
  const int duration_ms =
      std::max(1, static_cast<int>(duration_seconds * 1000.0));
  const double average_level =
      state.buffered_chunks > 0
          ? state.accumulated_level / static_cast<double>(state.buffered_chunks)
          : 0.0;
  const double average_norm = average_level / 32767.0;
  const double peak_norm = state.peak_level / 32767.0;

  std::ostringstream text;
  text.setf(std::ios::fixed);
  text.precision(2);

  if (!is_final) {
    text << "listening on device (" << duration_seconds << "s, avg "
         << average_norm << ")";
    AsrUpdate update;
    update.text = text.str();
    update.is_final = false;
    update.segment_index = state.utterance_index + 1;
    update.sample_rate_hz = sample_rate;
    update.duration_ms = duration_ms;
    update.average_level = average_norm;
    update.peak_level = peak_norm;
    update.end_reason = "streaming";
    return update;
  }

  const uint64_t segment_number = state.utterance_index + 1;
  text << (runtime_status_.asr_ready ? "speech segment " : "audio segment ")
       << segment_number
       << (runtime_status_.asr_ready ? " captured on device"
                                     : " received on device")
       << " (" << duration_seconds << "s, avg " << average_norm << ", peak "
       << peak_norm << ")";
  AsrUpdate update;
  update.text = text.str();
  update.is_final = true;
  update.segment_index = segment_number;
  update.sample_rate_hz = sample_rate;
  update.duration_ms = duration_ms;
  update.average_level = average_norm;
  update.peak_level = peak_norm;
  update.end_reason = state.finalize_reason;
  return update;
}

void SpeechPipeline::ResetPendingUtterance(SessionState* state,
                                           bool complete_turn) {
  if (state == nullptr) {
    return;
  }
  if (complete_turn && state->has_pending_audio) {
    state->utterance_index += 1;
  }
  state->has_pending_audio = false;
  state->buffered_samples = 0;
  state->buffered_chunks = 0;
  state->accumulated_level = 0.0;
  state->peak_level = 0.0;
  state->finalize_reason = "flush";
}

SpeechPipeline::SessionStatus
SpeechPipeline::BuildSessionStatus(const SessionState* state) const {
  SessionStatus snapshot;
  if (state == nullptr) {
    snapshot.detail =
        "No buffered speech state has been captured for this session yet.";
    return snapshot;
  }

  snapshot.available = true;
  snapshot.pending_audio = state->has_pending_audio;
  snapshot.interrupted = state->interrupted;
  snapshot.buffered_samples = state->buffered_samples;
  snapshot.buffered_chunks = state->buffered_chunks;
  snapshot.sample_rate_hz =
      state->sample_rate_hz > 0 ? state->sample_rate_hz : 16000;
  if (state->buffered_chunks > 0) {
    snapshot.average_level = (state->accumulated_level /
                              static_cast<double>(state->buffered_chunks)) /
                             32767.0;
  }
  snapshot.peak_level = state->peak_level / 32767.0;
  if (snapshot.pending_audio && snapshot.sample_rate_hz > 0) {
    snapshot.duration_ms = std::max(
        1, static_cast<int>((static_cast<double>(snapshot.buffered_samples) /
                             static_cast<double>(snapshot.sample_rate_hz)) *
                            1000.0));
  }
  snapshot.completed_segments = state->utterance_index;
  snapshot.current_segment_index =
      state->utterance_index + (state->has_pending_audio ? 1 : 0);
  snapshot.last_update_ms = state->last_update_ms;
  snapshot.finalize_reason = state->finalize_reason;

  if (state->interrupted) {
    snapshot.state = "interrupted";
    snapshot.detail =
        "Speech capture was interrupted before a final turn was emitted.";
  } else if (state->has_pending_audio) {
    snapshot.state = "capturing";
    std::ostringstream detail;
    detail << "Capturing speech segment " << snapshot.current_segment_index
           << " (" << snapshot.duration_ms << "ms buffered, avg " << std::fixed
           << std::setprecision(2) << snapshot.average_level << ", peak "
           << snapshot.peak_level << ").";
    snapshot.detail = detail.str();
  } else if (state->utterance_index > 0) {
    snapshot.state = "idle";
    snapshot.detail = "No buffered speech. Last completed segment: " +
                      std::to_string(state->utterance_index) + ".";
  } else {
    snapshot.state = "idle";
    snapshot.detail =
        "No buffered speech state has been captured for this session yet.";
  }

  return snapshot;
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
