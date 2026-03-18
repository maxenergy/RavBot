// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/mobile/mobile_engine.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <sstream>
#include <utility>

#include "ravbot/mobile/speech_pipeline.hpp"
#include "ravbot/providers/llama_cpp_mobile_provider.hpp"

namespace ravbot::mobile {

namespace {

constexpr char kDeviceStatusToolName[] = "device_status";
constexpr char kCameraSnapshotToolName[] = "camera_snapshot";
constexpr int kMaxMobileToolRounds = 4;

nlohmann::json make_avatar_payload(AvatarState state) {
  return {{"state", AvatarStateToString(state)}};
}

std::string make_tool_call_id(int round, size_t index) {
  return "mobile_tool_" + std::to_string(round) + "_" +
         std::to_string(index + 1);
}

class PlaceholderMobileVisionProvider : public MobileVisionProvider {
 public:
  explicit PlaceholderMobileVisionProvider(
      std::shared_ptr<spdlog::logger> logger)
      : logger_(std::move(logger)) {}

  std::optional<std::string> ObserveFrame(
      const CameraFrame& frame) override {
    if (logger_) {
      logger_->debug(
          "PlaceholderMobileVisionProvider accepted {}x{} {} frame",
          frame.width, frame.height, frame.format);
    }
    std::ostringstream stream;
    stream << "camera frame " << frame.width << "x" << frame.height << " "
           << frame.format << " received on device";
    return stream.str();
  }

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

std::shared_ptr<LLMProvider> make_default_text_provider(
    const RavBotConfig& config,
    const std::filesystem::path& models_dir,
    const std::shared_ptr<spdlog::logger>& logger) {
  if (!config.mobile.runtime.enabled || config.mobile.runtime.mode != "local") {
    return nullptr;
  }

  ravbot::LlamaCppMobileProvider::Options options;
  options.model_path = config.mobile.models.llm_model;
  options.vision_model_path = config.mobile.models.vlm_model;
  options.mmproj_path = config.mobile.models.mmproj_model;
  options.models_dir = models_dir.string();
  options.enable_vulkan = config.mobile.models.use_vulkan;
  return std::make_shared<ravbot::LlamaCppMobileProvider>(std::move(options),
                                                          logger);
}

std::string resolve_request_model(const RavBotConfig& config) {
  if (config.mobile.runtime.enabled && config.mobile.runtime.mode == "local" &&
      !config.mobile.models.llm_model.empty()) {
    return config.mobile.models.llm_model;
  }
  return config.agent.model;
}

}  // namespace

MobileEngine::MobileEngine(RavBotConfig config,
                           const std::filesystem::path& state_dir,
                           const std::filesystem::path& models_dir,
                           std::shared_ptr<spdlog::logger> logger)
    : config_(std::move(config)),
      logger_(std::move(logger)),
      session_manager_(state_dir / "sessions",
                       logger_ ? logger_ : spdlog::default_logger()),
      models_dir_(models_dir) {
  if (!logger_) {
    logger_ = spdlog::default_logger();
  }
  text_provider_ = make_default_text_provider(config_, models_dir_, logger_);
  if (config_.mobile.runtime.enabled) {
    speech_pipeline_ = std::make_shared<SpeechPipeline>(
        config_.mobile.models, models_dir_, logger_);
    asr_provider_ = speech_pipeline_;
    if (config_.mobile.vision.enabled) {
      vision_provider_ =
          std::make_shared<PlaceholderMobileVisionProvider>(logger_);
    }
  }
}

void MobileEngine::SetTextProvider(std::shared_ptr<LLMProvider> provider) {
  text_provider_ = std::move(provider);
}

void MobileEngine::SetAsrProvider(std::shared_ptr<MobileAsrProvider> provider) {
  asr_provider_ = std::move(provider);
}

void MobileEngine::SetVisionProvider(
    std::shared_ptr<MobileVisionProvider> provider) {
  vision_provider_ = std::move(provider);
}

void MobileEngine::SetDeviceBridge(
    std::shared_ptr<DeviceCapabilityBridge> bridge) {
  device_bridge_ = std::move(bridge);
}

std::string MobileEngine::SubscribeEvents(EventCallback callback) {
  std::unique_lock<std::shared_mutex> lock(subscriber_mutex_);
  std::string id = NextSubscriptionId();
  subscribers_[id] = std::move(callback);
  lock.unlock();
  EmitRuntimeStatus();
  std::optional<nlohmann::json> device_status_payload;
  {
    std::shared_lock<std::shared_mutex> state_lock(state_mutex_);
    if (has_device_status_) {
      device_status_payload = device_status_.ToJson();
    }
  }
  if (device_status_payload.has_value()) {
    Emit(kEventMobileDeviceStatus, *device_status_payload);
  }
  return id;
}

void MobileEngine::UnsubscribeEvents(const std::string& subscription_id) {
  std::unique_lock<std::shared_mutex> lock(subscriber_mutex_);
  subscribers_.erase(subscription_id);
}

std::string MobileEngine::StartSession(const std::string& session_key,
                                       const std::string& display_name) {
  auto handle = session_manager_.GetOrCreate(session_key, display_name, "mobile");
  EmitRuntimeStatus();
  AvatarState initial_state = AvatarState::kIdle;
  try {
    initial_state = AvatarStateFromString(config_.mobile.avatar.default_state);
  } catch (const std::exception&) {
    initial_state = AvatarState::kIdle;
  }
  SetAvatarState(initial_state);
  return handle.session_key;
}

bool MobileEngine::SendTextTurn(const std::string& session_key,
                                const std::string& text) {
  return HandleUserTextTurn(session_key, text, false);
}

bool MobileEngine::PushPcm16(const std::string& session_key,
                             const int16_t* samples,
                             size_t sample_count,
                             int sample_rate_hz,
                             bool end_of_turn) {
  if (!samples || sample_count == 0 || !asr_provider_) {
    return false;
  }

  SetAvatarState(AvatarState::kListen);
  AsrUpdate update =
      asr_provider_->PushPcm16(samples, sample_count, sample_rate_hz,
                               end_of_turn);
  return HandleAsrUpdate(session_key, update, sample_rate_hz);
}

bool MobileEngine::FlushAudioTurn(const std::string& session_key) {
  if (!asr_provider_) {
    return false;
  }

  SetAvatarState(AvatarState::kListen);
  AsrUpdate update = asr_provider_->Flush();
  if (update.text.empty()) {
    SetAvatarState(AvatarState::kIdle);
    return false;
  }
  return HandleAsrUpdate(session_key, update, config_.mobile.audio.sample_rate);
}

bool MobileEngine::PushCameraFrame(const std::string& session_key,
                                   const CameraFrame& frame) {
  if (!vision_provider_ || !config_.mobile.vision.enabled) {
    return false;
  }
  const bool foreground_only =
      config_.mobile.runtime.foreground_only ||
      config_.mobile.vision.foreground_only;
  if (foreground_only && !IsForeground()) {
    return false;
  }
  if (!ShouldProcessVisionFrame(frame)) {
    return true;
  }

  auto summary = vision_provider_->ObserveFrame(frame);
  if (!summary || summary->empty()) {
    return true;
  }

  session_manager_.AppendCustomMessage(
      session_key, "mobile_vision_observation",
      nlohmann::json::array(),
      nlohmann::json::object(),
      {{"summary", *summary},
       {"width", frame.width},
       {"height", frame.height},
       {"format", frame.format},
       {"timestampMs", frame.timestamp_ms}});

  {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    last_vision_observation_ = {{"summary", *summary},
                                {"width", frame.width},
                                {"height", frame.height},
                                {"format", frame.format},
                                {"timestampMs", frame.timestamp_ms}};
    has_last_vision_observation_ = true;
  }

  SetAvatarState(AvatarState::kWatch);
  Emit(kEventMobileVisionObservation,
       {{"summary", *summary},
        {"width", frame.width},
        {"height", frame.height},
        {"timestampMs", frame.timestamp_ms}});
  SetAvatarState(AvatarState::kIdle);
  return true;
}

void MobileEngine::InterruptGeneration(const std::string& session_key) {
  (void)session_key;
  if (asr_provider_) {
    asr_provider_->Interrupt();
  }
  if (device_bridge_) {
    device_bridge_->InterruptSpeechPlayback();
  }
  Emit(kEventMobileTtsState, {{"state", "interrupted"}});
  SetAvatarState(AvatarState::kIdle);
}

bool MobileEngine::ReportTtsPlaybackState(const std::string& session_key,
                                          const std::string& state) {
  (void)session_key;
  if (state.empty()) {
    return false;
  }

  Emit(kEventMobileTtsState, {{"state", state}});
  if (state == "speaking") {
    SetAvatarState(AvatarState::kSpeak);
  } else if (state == "completed" || state == "interrupted") {
    SetAvatarState(AvatarState::kIdle);
  } else if (state == "error") {
    SetAvatarState(AvatarState::kError);
  }
  return true;
}

bool MobileEngine::ReportDeviceStatus(DeviceStatusSnapshot status) {
  nlohmann::json payload;
  {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    status.foreground = foreground_;
    device_status_ = std::move(status);
    has_device_status_ = true;
    payload = device_status_.ToJson();
  }

  Emit(kEventMobileDeviceStatus, payload);
  return true;
}

void MobileEngine::SetForegroundState(bool foreground) {
  std::optional<nlohmann::json> device_status_payload;
  std::unique_lock<std::shared_mutex> lock(state_mutex_);
  foreground_ = foreground;
  if (has_device_status_) {
    device_status_.foreground = foreground_;
    device_status_payload = device_status_.ToJson();
  }
  lock.unlock();
  if (device_status_payload.has_value()) {
    Emit(kEventMobileDeviceStatus, *device_status_payload);
  }
}

bool MobileEngine::IsForeground() const {
  std::shared_lock<std::shared_mutex> lock(state_mutex_);
  return foreground_;
}

void MobileEngine::Emit(const std::string& event_name,
                        const nlohmann::json& payload) const {
  MobileEvent event{event_name, payload};

  std::shared_lock<std::shared_mutex> lock(subscriber_mutex_);
  for (const auto& [id, callback] : subscribers_) {
    (void)id;
    callback(event);
  }
}

void MobileEngine::EmitRuntimeStatus() const {
  nlohmann::json payload = {{"modelsDir", models_dir_.string()},
                            {"sttModel", config_.mobile.models.stt_model},
                            {"ttsVoice", config_.mobile.models.tts_voice},
                            {"visionEnabled", config_.mobile.vision.enabled},
                            {"continuousVision",
                             config_.mobile.runtime.continuous_vision}};

  if (auto* llama_provider =
          dynamic_cast<ravbot::LlamaCppMobileProvider*>(text_provider_.get())) {
    payload.update(llama_provider->runtime_status().ToJson());
  } else if (text_provider_) {
    payload["provider"] = text_provider_->GetProviderName();
    payload["ready"] = true;
    payload["detail"] =
        "Runtime diagnostics are only implemented for llama.cpp mobile.";
  } else {
    payload["provider"] = "none";
    payload["ready"] = false;
    payload["detail"] = "No local mobile text provider is configured.";
  }

  if (speech_pipeline_) {
    payload.update(speech_pipeline_->runtime_status().ToJson());
  } else {
    payload["speechProvider"] = "none";
    payload["speechDetail"] = "No local speech pipeline is configured.";
  }

  Emit(kEventMobileRuntimeStatus, payload);
}

void MobileEngine::SetAvatarState(AvatarState state) {
  {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    avatar_state_ = state;
  }
  if (device_bridge_) {
    device_bridge_->SetAvatarState(state);
  }
  Emit(kEventMobileAvatarState, make_avatar_payload(state));
}

bool MobileEngine::ShouldProcessVisionFrame(const CameraFrame& frame) {
  std::unique_lock<std::shared_mutex> lock(state_mutex_);

  const double sample_fps = config_.mobile.vision.sample_fps;
  if (sample_fps > 0.0 && frame.timestamp_ms > 0 &&
      last_vision_timestamp_ms_ > 0) {
    const auto min_interval_ms =
        static_cast<int64_t>(std::max(1.0, 1000.0 / sample_fps));
    if (frame.timestamp_ms - last_vision_timestamp_ms_ < min_interval_ms) {
      return false;
    }
  }

  const auto scene_signature = EstimateFrameSceneSignature(frame);
  if (scene_signature && last_vision_scene_signature_ &&
      config_.mobile.vision.scene_change_threshold > 0.0) {
    const double delta =
        std::abs(*scene_signature - *last_vision_scene_signature_);
    if (delta < config_.mobile.vision.scene_change_threshold) {
      return false;
    }
  }

  if (frame.timestamp_ms > 0) {
    last_vision_timestamp_ms_ = frame.timestamp_ms;
  }
  if (scene_signature) {
    last_vision_scene_signature_ = scene_signature;
  }
  return true;
}

std::optional<double> MobileEngine::EstimateFrameSceneSignature(
    const CameraFrame& frame) const {
  if (frame.data.empty()) {
    return std::nullopt;
  }

  size_t sample_limit = frame.data.size();
  if (frame.width > 0 && frame.height > 0 &&
      (frame.format == "YUV420_LUMA" || frame.format == "yuv420_luma" ||
       frame.format == "NV21" || frame.format == "nv21")) {
    sample_limit = std::min(
        sample_limit,
        static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height));
  }

  if (sample_limit == 0) {
    return std::nullopt;
  }

  const size_t step = std::max<size_t>(1, sample_limit / 512);
  uint64_t total = 0;
  size_t count = 0;
  for (size_t i = 0; i < sample_limit; i += step) {
    total += frame.data[i];
    ++count;
  }
  if (count == 0) {
    return std::nullopt;
  }
  return static_cast<double>(total) /
         (255.0 * static_cast<double>(count));
}

bool MobileEngine::HandleAsrUpdate(const std::string& session_key,
                                   const AsrUpdate& update,
                                   int sample_rate_hz) {
  if (update.text.empty()) {
    return true;
  }

  const int effective_sample_rate =
      update.sample_rate_hz > 0 ? update.sample_rate_hz : sample_rate_hz;
  Emit(update.is_final ? kEventMobileAsrFinal : kEventMobileAsrPartial,
       {{"text", update.text},
        {"sampleRateHz", effective_sample_rate},
        {"segmentIndex", update.segment_index},
        {"durationMs", update.duration_ms},
        {"averageLevel", update.average_level},
        {"peakLevel", update.peak_level},
        {"endReason", update.end_reason}});
  if (!update.is_final) {
    return true;
  }

  return HandleUserTextTurn(session_key, update.text, false);
}

std::vector<nlohmann::json> MobileEngine::BuildToolSchemas() const {
  return {nlohmann::json{
      {"type", "function"},
      {"function",
       {{"name", kDeviceStatusToolName},
        {"description",
         "Get the current Android host device status including foreground "
         "state, service state, capture state, permissions, microphone, "
         "camera, and speaker status."},
        {"parameters",
         {{"type", "object"},
          {"properties", nlohmann::json::object()},
          {"additionalProperties", false}}}}}},
      nlohmann::json{
          {"type", "function"},
          {"function",
           {{"name", kCameraSnapshotToolName},
            {"description",
             "Get the latest on-device camera observation summary and frame "
             "metadata captured by the Android host."},
            {"parameters",
             {{"type", "object"},
              {"properties", nlohmann::json::object()},
              {"additionalProperties", false}}}}}}};
}

std::string MobileEngine::BuildDeviceStatusToolResult() const {
  nlohmann::json result;
  {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    result["available"] = has_device_status_;
    if (has_device_status_) {
      result["deviceStatus"] = device_status_.ToJson();
    } else {
      DeviceStatusSnapshot fallback_status;
      fallback_status.foreground = foreground_;
      result["deviceStatus"] = fallback_status.ToJson();
      result["detail"] =
          "Host has not published a live device status snapshot yet.";
    }
  }
  return result.dump(2);
}

std::string MobileEngine::BuildCameraSnapshotToolResult() const {
  nlohmann::json result;
  {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    result["available"] = has_last_vision_observation_;
    if (has_last_vision_observation_) {
      result["observation"] = last_vision_observation_;
    } else {
      result["detail"] =
          "No camera observation has been captured in this session yet.";
    }
    result["visionEnabled"] = config_.mobile.vision.enabled;
    result["foregroundOnly"] =
        config_.mobile.runtime.foreground_only ||
        config_.mobile.vision.foreground_only;
  }
  return result.dump(2);
}

std::string MobileEngine::ExecuteToolCall(const ToolCall& tool_call) const {
  if (tool_call.name == kDeviceStatusToolName) {
    return BuildDeviceStatusToolResult();
  }
  if (tool_call.name == kCameraSnapshotToolName) {
    return BuildCameraSnapshotToolResult();
  }
  throw std::runtime_error("Unsupported mobile tool: " + tool_call.name);
}

bool MobileEngine::HandleUserTextTurn(const std::string& session_key,
                                      const std::string& text,
                                      bool emit_asr_final) {
  if (text.empty()) {
    return false;
  }

  session_manager_.GetOrCreate(session_key, "", "mobile");
  session_manager_.AppendMessage(session_key, "user", text);
  if (emit_asr_final) {
    Emit(kEventMobileAsrFinal, {{"text", text}});
  }

  SetAvatarState(AvatarState::kThink);

  std::string response_text;
  std::string finish_reason = "stop";

  if (text_provider_) {
    try {
      for (int round = 0; round < kMaxMobileToolRounds; ++round) {
        ChatCompletionRequest request;
        request.messages = BuildRequestMessages(session_key);
        request.model = resolve_request_model(config_);
        request.temperature = config_.agent.temperature;
        request.max_tokens = config_.agent.max_tokens;
        request.tools = BuildToolSchemas();
        request.stream = true;

        std::vector<std::string> delta_chunks;
        std::vector<ToolCall> streamed_tool_calls;
        std::unordered_set<std::string> seen_tool_call_ids;
        std::string round_response_text;
        std::string round_finish_reason = "stop";

        text_provider_->ChatCompletionStream(
            request, [&](const ChatCompletionResponse& chunk) {
              if (!chunk.content.empty()) {
                round_response_text += chunk.content;
                delta_chunks.push_back(chunk.content);
              }
              for (const auto& raw_tool_call : chunk.tool_calls) {
                ToolCall tool_call = raw_tool_call;
                const std::string dedupe_key =
                    tool_call.id.empty()
                        ? tool_call.name + ":" + tool_call.arguments.dump()
                        : tool_call.id;
                if (seen_tool_call_ids.insert(dedupe_key).second) {
                  streamed_tool_calls.push_back(std::move(tool_call));
                }
              }
              if (!chunk.finish_reason.empty()) {
                round_finish_reason = chunk.finish_reason;
              } else if (chunk.is_stream_end && round_finish_reason.empty()) {
                round_finish_reason = "stop";
              }
            });

        if (streamed_tool_calls.empty() && round_finish_reason != "tool_calls") {
          response_text = round_response_text;
          finish_reason = round_finish_reason;
          for (const auto& chunk : delta_chunks) {
            Emit(kEventAssistantDelta, {{"text", chunk}});
          }
          break;
        }

        SessionMessage assistant_tool_use;
        assistant_tool_use.role = "assistant";
        if (!round_response_text.empty()) {
          assistant_tool_use.content.push_back(
              ContentBlock::MakeText(round_response_text));
        }
        for (size_t i = 0; i < streamed_tool_calls.size(); ++i) {
          if (streamed_tool_calls[i].id.empty()) {
            streamed_tool_calls[i].id = make_tool_call_id(round, i);
          }
          assistant_tool_use.content.push_back(ContentBlock::MakeToolUse(
              streamed_tool_calls[i].id, streamed_tool_calls[i].name,
              streamed_tool_calls[i].arguments));
        }
        if (!assistant_tool_use.content.empty()) {
          session_manager_.AppendMessage(session_key, assistant_tool_use);
        }

        SessionMessage tool_result_message;
        tool_result_message.role = "user";
        for (const auto& tool_call : streamed_tool_calls) {
          Emit(kEventToolStart,
               {{"id", tool_call.id},
                {"name", tool_call.name},
                {"arguments", tool_call.arguments}});

          try {
            const std::string result = ExecuteToolCall(tool_call);
            tool_result_message.content.push_back(
                ContentBlock::MakeToolResult(tool_call.id, result));
            Emit(kEventToolResult,
                 {{"id", tool_call.id},
                  {"name", tool_call.name},
                  {"status", "ok"},
                  {"result", result}});
          } catch (const std::exception& e) {
            const std::string error = e.what();
            tool_result_message.content.push_back(
                ContentBlock::MakeToolResult(tool_call.id, error));
            Emit(kEventToolResult,
                 {{"id", tool_call.id},
                  {"name", tool_call.name},
                  {"status", "error"},
                  {"error", error}});
          }
        }

        if (!tool_result_message.content.empty()) {
          session_manager_.AppendMessage(session_key, tool_result_message);
          finish_reason = "tool_calls";
          continue;
        }

        response_text = round_response_text;
        finish_reason = round_finish_reason;
        for (const auto& chunk : delta_chunks) {
          Emit(kEventAssistantDelta, {{"text", chunk}});
        }
        break;
      }
    } catch (const std::exception& e) {
      logger_->error("MobileEngine text generation failed: {}", e.what());
      response_text = std::string("Mobile generation failed: ") + e.what();
      finish_reason = "error";
    }
  } else {
    response_text = "Mobile local provider is not configured.";
    finish_reason = "error";
  }

  if (finish_reason == "tool_calls" && response_text.empty()) {
    response_text =
        "Mobile tool loop reached the maximum number of tool rounds.";
    finish_reason = "error";
  }

  session_manager_.AppendMessage(session_key, "assistant", response_text);
  Emit(kEventAssistantFinal,
       {{"text", response_text}, {"finishReason", finish_reason}});

  if (!response_text.empty()) {
    SetAvatarState(AvatarState::kSpeak);
    Emit(kEventMobileTtsState,
         {{"state", "requested"}, {"text", response_text}});
    if (device_bridge_) {
      device_bridge_->RequestSpeechPlayback(response_text);
    }
  }

  SetAvatarState(finish_reason == "error" ? AvatarState::kError
                                          : AvatarState::kIdle);
  return true;
}

std::vector<Message> MobileEngine::BuildRequestMessages(
    const std::string& session_key) const {
  std::vector<Message> messages;
  for (const auto& entry : session_manager_.GetHistory(session_key)) {
    Message message;
    message.role = entry.role;
    message.content = entry.content;
    message.timestamp = entry.timestamp;
    messages.push_back(std::move(message));
  }
  return messages;
}

std::string MobileEngine::NextSubscriptionId() {
  return std::to_string(next_subscription_id_++);
}

}  // namespace ravbot::mobile
