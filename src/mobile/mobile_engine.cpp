// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/mobile/mobile_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <fstream>
#include <iomanip>
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
constexpr char kTimeToolName[] = "time";
constexpr char kWebSearchToolName[] = "web_search";
constexpr char kWebFetchToolName[] = "web_fetch";
constexpr char kMemoryListToolName[] = "memory_list";
constexpr char kMemorySearchToolName[] = "memory_search";
constexpr char kMemoryGetToolName[] = "memory_get";
constexpr char kMemoryWriteToolName[] = "memory_write";
constexpr char kMemoryDeleteToolName[] = "memory_delete";
constexpr int kMaxMobileToolRounds = 4;

nlohmann::json make_avatar_payload(AvatarState state) {
  return {{"state", AvatarStateToString(state)}};
}

std::string make_tool_call_id(int round, size_t index) {
  return "mobile_tool_" + std::to_string(round) + "_" +
         std::to_string(index + 1);
}

std::tm utc_tm(std::time_t timestamp) {
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &timestamp);
#else
  gmtime_r(&timestamp, &tm);
#endif
  return tm;
}

std::tm local_tm(std::time_t timestamp) {
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &timestamp);
#else
  localtime_r(&timestamp, &tm);
#endif
  return tm;
}

std::string format_tm(const std::tm& tm) {
  std::ostringstream stream;
  stream << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
  return stream.str();
}

std::string format_timezone_name(const std::tm& tm) {
  std::ostringstream stream;
  stream << std::put_time(&tm, "%Z");
  return stream.str();
}

nlohmann::json make_workspace_entry_json(
    const std::filesystem::path& workspace,
    const std::filesystem::path& full_path) {
  std::error_code ec;
  const auto relative = std::filesystem::relative(full_path, workspace, ec);
  const std::string relative_path =
      ec ? full_path.filename().generic_string() : relative.generic_string();
  const bool is_directory = std::filesystem::is_directory(full_path, ec);

  nlohmann::json entry{{"path", relative_path},
                       {"kind", is_directory ? "directory" : "file"}};
  if (!is_directory) {
    entry["sizeBytes"] = std::filesystem::file_size(full_path, ec);
    if (ec) {
      entry["sizeBytes"] = 0;
    }
  }
  return entry;
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
      state_dir_(state_dir),
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
  std::unique_lock<std::shared_mutex> lock(state_mutex_);
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
  if (auto bridge = CopyDeviceBridge(); bridge) {
    bridge->InterruptSpeechPlayback();
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
  const auto bridge = CopyDeviceBridge();
  const bool device_bridge_attached = bridge != nullptr;
  const bool web_search_ready =
      bridge != nullptr && bridge->SupportsWebSearch();
  const bool web_fetch_ready = bridge != nullptr && bridge->SupportsWebFetch();
  nlohmann::json payload = {{"modelsDir", models_dir_.string()},
                            {"sttModel", config_.mobile.models.stt_model},
                            {"ttsVoice", config_.mobile.models.tts_voice},
                            {"visionEnabled", config_.mobile.vision.enabled},
                            {"continuousVision",
                             config_.mobile.runtime.continuous_vision},
                            {"deviceBridgeAttached", device_bridge_attached},
                            {"webSearchReady", web_search_ready},
                            {"webFetchReady", web_fetch_ready}};

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
  if (auto bridge = CopyDeviceBridge(); bridge) {
    bridge->SetAvatarState(state);
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
  const auto bridge = CopyDeviceBridge();
  const bool web_search_ready =
      bridge != nullptr && bridge->SupportsWebSearch();
  const bool web_fetch_ready = bridge != nullptr && bridge->SupportsWebFetch();
  std::vector<nlohmann::json> tools = {nlohmann::json{
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
              {"additionalProperties", false}}}}}},
      nlohmann::json{
          {"type", "function"},
          {"function",
           {{"name", kTimeToolName},
            {"description",
             "Get the current device time in local time and UTC, including "
             "timezone offset and epoch milliseconds."},
            {"parameters",
             {{"type", "object"},
              {"properties", nlohmann::json::object()},
              {"additionalProperties", false}}}}}},
      nlohmann::json{
          {"type", "function"},
          {"function",
           {{"name", kMemoryListToolName},
            {"description",
             "List files and directories in the mobile memory workspace or "
             "under a specific subdirectory."},
            {"parameters",
             {{"type", "object"},
              {"properties",
               {{"path",
                 {{"type", "string"},
                  {"description",
                   "Optional subdirectory inside the mobile memory "
                   "workspace. Empty means the workspace root."}}},
                {"recursive",
                 {{"type", "boolean"},
                  {"description",
                   "Whether to recursively include nested directories."}}},
                {"maxEntries",
                 {{"type", "integer"},
                  {"minimum", 1},
                  {"maximum", 500},
                  {"description",
                   "Maximum number of entries to return."}}}}},
              {"additionalProperties", false}}}}}},
      nlohmann::json{
          {"type", "function"},
          {"function",
           {{"name", kMemorySearchToolName},
            {"description",
             "Search the mobile workspace memory files stored on device."},
            {"parameters",
             {{"type", "object"},
              {"properties",
               {{"query",
                 {{"type", "string"},
                  {"description", "Search query for mobile memory."}}},
                {"maxResults",
                 {{"type", "integer"},
                  {"description", "Maximum number of matches to return."}}}}},
              {"required", {"query"}},
              {"additionalProperties", false}}}}}},
      nlohmann::json{
          {"type", "function"},
          {"function",
           {{"name", kMemoryGetToolName},
            {"description",
             "Read a specific file from the mobile workspace memory."},
            {"parameters",
             {{"type", "object"},
              {"properties",
               {{"path",
                 {{"type", "string"},
                  {"description", "Relative path inside mobile workspace."}}}}},
              {"required", {"path"}},
              {"additionalProperties", false}}}}}},
      nlohmann::json{
          {"type", "function"},
          {"function",
           {{"name", kMemoryWriteToolName},
            {"description",
             "Write or append content to a file in the mobile workspace "
             "memory."},
            {"parameters",
             {{"type", "object"},
              {"properties",
               {{"path",
                 {{"type", "string"},
                  {"description", "Relative path inside mobile workspace."}}},
                {"content",
                 {{"type", "string"},
                  {"description", "Content to write."}}},
                {"mode",
                 {{"type", "string"},
                  {"enum", {"overwrite", "append"}},
                  {"description", "Write mode."}}}}},
              {"required", {"path", "content"}},
              {"additionalProperties", false}}}}}},
      nlohmann::json{
          {"type", "function"},
          {"function",
           {{"name", kMemoryDeleteToolName},
            {"description",
             "Delete a file or directory from the mobile memory workspace."},
            {"parameters",
             {{"type", "object"},
              {"properties",
               {{"path",
                 {{"type", "string"},
                  {"description",
                   "Relative file or directory path inside the mobile memory "
                   "workspace."}}},
                {"recursive",
                 {{"type", "boolean"},
                  {"description",
                   "Required when deleting a non-empty directory."}}}}},
              {"required", {"path"}},
              {"additionalProperties", false}}}}}}};
  if (web_search_ready) {
    tools.push_back(nlohmann::json{
        {"type", "function"},
        {"function",
         {{"name", kWebSearchToolName},
          {"description",
           "Search the web through the Android host networking stack and "
           "return titles, URLs, and snippets."},
          {"parameters",
           {{"type", "object"},
            {"properties",
             {{"query",
               {{"type", "string"},
                {"description", "Search query."}}},
              {"count",
               {{"type", "integer"},
                {"minimum", 1},
                {"maximum", 10},
                {"description", "Maximum number of search results."}}},
              {"freshness",
               {{"type", "string"},
                {"enum", {"day", "week", "month", "year"}},
                {"description", "Optional recency filter."}}}}},
            {"required", {"query"}},
            {"additionalProperties", false}}}}}});
  }
  if (web_fetch_ready) {
    tools.push_back(nlohmann::json{
        {"type", "function"},
        {"function",
         {{"name", kWebFetchToolName},
          {"description",
           "Fetch a web page through the Android host networking stack and "
           "return plain text content."},
          {"parameters",
           {{"type", "object"},
            {"properties",
             {{"url",
               {{"type", "string"},
                {"description", "Absolute http or https URL to fetch."}}},
              {"maxChars",
               {{"type", "integer"},
                {"minimum", 256},
                {"maximum", 100000},
                {"description", "Maximum number of characters to return."}}}}},
            {"required", {"url"}},
            {"additionalProperties", false}}}}}});
  }
  return tools;
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

std::string MobileEngine::BuildTimeToolResult() const {
  const auto now = std::chrono::system_clock::now();
  const auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count();
  const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
  const std::tm utc = utc_tm(timestamp);
  const std::tm local = local_tm(timestamp);

  std::tm local_copy = local;
  std::tm utc_copy = utc;
  std::time_t local_as_time = std::mktime(&local_copy);
  std::time_t utc_as_time = std::mktime(&utc_copy);
  const int offset_seconds =
      static_cast<int>(std::difftime(local_as_time, utc_as_time));
  const int offset_minutes = offset_seconds / 60;

  std::ostringstream offset_stream;
  const char sign = offset_minutes >= 0 ? '+' : '-';
  const int absolute_minutes = std::abs(offset_minutes);
  offset_stream << sign << std::setw(2) << std::setfill('0')
                << (absolute_minutes / 60) << ":" << std::setw(2)
                << (absolute_minutes % 60);

  return nlohmann::json{
      {"epochMs", epoch_ms},
      {"utc", format_tm(utc) + "Z"},
      {"local", format_tm(local)},
      {"utcOffsetMinutes", offset_minutes},
      {"utcOffset", offset_stream.str()},
      {"timezoneName", format_timezone_name(local)}}
      .dump(2);
}

std::string MobileEngine::BuildWebSearchToolResult(
    const nlohmann::json& arguments) const {
  const auto bridge = CopyDeviceBridge();
  if (bridge == nullptr) {
    throw std::runtime_error("web_search requires a device bridge");
  }
  if (!bridge->SupportsWebSearch()) {
    throw std::runtime_error("web_search is not supported by the device bridge");
  }
  const std::string query = arguments.value("query", "");
  const int count = arguments.value("count", 5);
  const std::string freshness = arguments.value("freshness", "");
  if (query.empty()) {
    throw std::runtime_error("query is required");
  }
  return bridge->WebSearch(query, count, freshness);
}

std::string MobileEngine::BuildWebFetchToolResult(
    const nlohmann::json& arguments) const {
  const auto bridge = CopyDeviceBridge();
  if (bridge == nullptr) {
    throw std::runtime_error("web_fetch requires a device bridge");
  }
  if (!bridge->SupportsWebFetch()) {
    throw std::runtime_error("web_fetch is not supported by the device bridge");
  }
  const std::string url = arguments.value("url", "");
  const int max_chars = arguments.value("maxChars", 50000);
  if (url.empty()) {
    throw std::runtime_error("url is required");
  }
  return bridge->WebFetch(url, max_chars);
}

std::filesystem::path MobileEngine::WorkspaceRoot() const {
  return state_dir_ / "workspace";
}

std::filesystem::path MobileEngine::ResolveWorkspacePath(
    const std::string& relative_path) const {
  if (relative_path.empty()) {
    throw std::runtime_error("path is required");
  }

  const std::filesystem::path workspace = WorkspaceRoot().lexically_normal();
  const std::filesystem::path resolved =
      (workspace / std::filesystem::path(relative_path)).lexically_normal();

  const std::string workspace_str = workspace.generic_string();
  const std::string resolved_str = resolved.generic_string();
  const bool inside_workspace =
      resolved_str == workspace_str ||
      resolved_str.rfind(workspace_str + "/", 0) == 0;
  if (!inside_workspace) {
    throw std::runtime_error("Access denied: path outside mobile workspace");
  }
  return resolved;
}

std::shared_ptr<DeviceCapabilityBridge> MobileEngine::CopyDeviceBridge() const {
  std::shared_lock<std::shared_mutex> lock(state_mutex_);
  return device_bridge_;
}

std::string MobileEngine::BuildMemoryListToolResult(
    const nlohmann::json& arguments) const {
  const std::string relative_path = arguments.value("path", "");
  const bool recursive = arguments.value("recursive", false);
  const size_t max_entries =
      static_cast<size_t>(arguments.value("maxEntries", 100));

  const std::filesystem::path workspace = WorkspaceRoot();
  std::filesystem::create_directories(workspace);
  const std::filesystem::path full_path =
      relative_path.empty() ? workspace : ResolveWorkspacePath(relative_path);
  if (!std::filesystem::exists(full_path)) {
    throw std::runtime_error("Path not found: " + relative_path);
  }

  std::vector<nlohmann::json> entries;
  if (std::filesystem::is_regular_file(full_path)) {
    entries.push_back(make_workspace_entry_json(workspace, full_path));
  } else if (std::filesystem::is_directory(full_path)) {
    if (recursive) {
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(full_path)) {
        entries.push_back(make_workspace_entry_json(workspace, entry.path()));
        if (entries.size() >= max_entries) {
          break;
        }
      }
    } else {
      for (const auto& entry : std::filesystem::directory_iterator(full_path)) {
        entries.push_back(make_workspace_entry_json(workspace, entry.path()));
        if (entries.size() >= max_entries) {
          break;
        }
      }
    }
    std::sort(entries.begin(), entries.end(),
              [](const nlohmann::json& lhs, const nlohmann::json& rhs) {
                return lhs.value("path", "") < rhs.value("path", "");
              });
  } else {
    throw std::runtime_error("Unsupported path type: " + relative_path);
  }

  return nlohmann::json{{"workspace", workspace.string()},
                        {"path", relative_path},
                        {"recursive", recursive},
                        {"count", entries.size()},
                        {"entries", entries}}
      .dump(2);
}

std::string MobileEngine::BuildMemorySearchToolResult(
    const nlohmann::json& arguments) const {
  const std::string query = arguments.value("query", "");
  const int max_results = arguments.value("maxResults", 10);
  if (query.empty()) {
    throw std::runtime_error("query is required");
  }

  const std::filesystem::path workspace = WorkspaceRoot();
  std::filesystem::create_directories(workspace);

  MemorySearch search(logger_);
  search.IndexDirectory(workspace);
  const auto results = search.Search(query, max_results);

  nlohmann::json arr = nlohmann::json::array();
  for (const auto& result : results) {
    std::filesystem::path source_path(result.source);
    std::string source = source_path.lexically_normal().generic_string();
    std::error_code ec;
    const auto relative = std::filesystem::relative(source_path, workspace, ec);
    if (!ec && !relative.empty()) {
      source = relative.generic_string();
    }
    arr.push_back({{"source", source},
                   {"content", result.content},
                   {"score", result.score},
                   {"lineNumber", result.line_number}});
  }

  return nlohmann::json{{"workspace", workspace.string()},
                        {"query", query},
                        {"count", arr.size()},
                        {"results", arr}}
      .dump(2);
}

std::string MobileEngine::BuildMemoryGetToolResult(
    const nlohmann::json& arguments) const {
  const std::string relative_path = arguments.value("path", "");
  const std::filesystem::path full_path = ResolveWorkspacePath(relative_path);
  if (!std::filesystem::exists(full_path)) {
    throw std::runtime_error("File not found: " + relative_path);
  }

  std::ifstream file(full_path);
  if (!file) {
    throw std::runtime_error("Cannot read: " + relative_path);
  }
  return nlohmann::json{{"path", relative_path},
                        {"content", std::string(std::istreambuf_iterator<char>(file), {})}}
      .dump(2);
}

std::string MobileEngine::BuildMemoryWriteToolResult(
    const nlohmann::json& arguments) const {
  const std::string relative_path = arguments.value("path", "");
  const std::string content = arguments.value("content", "");
  const std::string mode = arguments.value("mode", "overwrite");
  if (relative_path.empty()) {
    throw std::runtime_error("path is required");
  }
  if (content.empty()) {
    throw std::runtime_error("content is required");
  }
  if (mode != "overwrite" && mode != "append") {
    throw std::runtime_error("mode must be 'overwrite' or 'append'");
  }

  const std::filesystem::path full_path = ResolveWorkspacePath(relative_path);
  std::filesystem::create_directories(full_path.parent_path());

  std::ios_base::openmode open_mode = std::ios::out;
  if (mode == "append") {
    open_mode |= std::ios::app;
  }

  std::ofstream output(full_path, open_mode);
  if (!output) {
    throw std::runtime_error("Failed to open file: " + relative_path);
  }
  output << content;
  if (mode == "append" && !content.empty() && content.back() != '\n') {
    output << '\n';
  }

  return nlohmann::json{{"ok", true},
                        {"path", relative_path},
                        {"mode", mode},
                        {"bytesWritten", content.size()}}
      .dump(2);
}

std::string MobileEngine::BuildMemoryDeleteToolResult(
    const nlohmann::json& arguments) const {
  const std::string relative_path = arguments.value("path", "");
  const bool recursive = arguments.value("recursive", false);
  if (relative_path.empty()) {
    throw std::runtime_error("path is required");
  }

  const std::filesystem::path workspace = WorkspaceRoot().lexically_normal();
  const std::filesystem::path full_path =
      ResolveWorkspacePath(relative_path).lexically_normal();
  if (full_path == workspace) {
    throw std::runtime_error("Refusing to delete mobile workspace root");
  }
  if (!std::filesystem::exists(full_path)) {
    throw std::runtime_error("Path not found: " + relative_path);
  }

  const bool is_directory = std::filesystem::is_directory(full_path);
  uintmax_t removed_count = 0;
  if (is_directory) {
    if (recursive) {
      removed_count = std::filesystem::remove_all(full_path);
    } else {
      if (!std::filesystem::is_empty(full_path)) {
        throw std::runtime_error(
            "directory delete requires recursive=true when not empty");
      }
      removed_count = std::filesystem::remove(full_path) ? 1 : 0;
    }
  } else {
    removed_count = std::filesystem::remove(full_path) ? 1 : 0;
  }

  return nlohmann::json{{"ok", removed_count > 0},
                        {"path", relative_path},
                        {"kind", is_directory ? "directory" : "file"},
                        {"recursive", recursive},
                        {"removedCount", removed_count}}
      .dump(2);
}

std::string MobileEngine::ExecuteToolCall(const ToolCall& tool_call) const {
  if (tool_call.name == kDeviceStatusToolName) {
    return BuildDeviceStatusToolResult();
  }
  if (tool_call.name == kCameraSnapshotToolName) {
    return BuildCameraSnapshotToolResult();
  }
  if (tool_call.name == kTimeToolName) {
    return BuildTimeToolResult();
  }
  if (tool_call.name == kWebSearchToolName) {
    return BuildWebSearchToolResult(tool_call.arguments);
  }
  if (tool_call.name == kWebFetchToolName) {
    return BuildWebFetchToolResult(tool_call.arguments);
  }
  if (tool_call.name == kMemoryListToolName) {
    return BuildMemoryListToolResult(tool_call.arguments);
  }
  if (tool_call.name == kMemorySearchToolName) {
    return BuildMemorySearchToolResult(tool_call.arguments);
  }
  if (tool_call.name == kMemoryGetToolName) {
    return BuildMemoryGetToolResult(tool_call.arguments);
  }
  if (tool_call.name == kMemoryWriteToolName) {
    return BuildMemoryWriteToolResult(tool_call.arguments);
  }
  if (tool_call.name == kMemoryDeleteToolName) {
    return BuildMemoryDeleteToolResult(tool_call.arguments);
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
    if (auto bridge = CopyDeviceBridge(); bridge) {
      bridge->RequestSpeechPlayback(response_text);
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
