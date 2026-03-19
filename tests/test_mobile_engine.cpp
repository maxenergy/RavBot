// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/sinks/null_sink.h>

#include "ravbot/mobile/mobile_engine.hpp"

#include <gtest/gtest.h>

namespace {

class FakeTextProvider : public ravbot::LLMProvider {
 public:
  explicit FakeTextProvider(std::string reply) : reply_(std::move(reply)) {}

  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    last_request_ = request;
    ravbot::ChatCompletionResponse response;
    response.content = reply_;
    response.finish_reason = "stop";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    callback(ChatCompletion(request));
  }

  std::string GetProviderName() const override {
    return "fake";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-model"};
  }

  ravbot::ChatCompletionRequest last_request_;

 private:
  std::string reply_;
};

class FakeStreamingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    last_request_ = request;
    ravbot::ChatCompletionResponse response;
    response.content = "unused";
    response.finish_reason = "stop";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    last_request_ = request;

    ravbot::ChatCompletionResponse first;
    first.content = "hello ";
    callback(first);

    ravbot::ChatCompletionResponse second;
    second.content = "stream";
    second.finish_reason = "stop";
    second.is_stream_end = true;
    callback(second);
  }

  std::string GetProviderName() const override {
    return "fake-stream";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-stream-model"};
  }

  ravbot::ChatCompletionRequest last_request_;
};

class FakeToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result") {
        response.content =
            "Device status received. Service is running and speaker is "
            "speaking.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_device_status";
    tool_call.name = "device_status";
    tool_call.arguments = nlohmann::json::object();
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-tool";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-tool-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeCameraToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("desk with phone") !=
              std::string::npos) {
        response.content = "Latest camera observation shows a desk with phone.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_camera_snapshot";
    tool_call.name = "camera_snapshot";
    tool_call.arguments = nlohmann::json::object();
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-camera-tool";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-camera-tool-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeMissingCameraToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("\"available\": false") !=
              std::string::npos &&
          last.content.front().content.find("\"reason\": \"no_frame_yet\"") !=
              std::string::npos) {
        response.content = "No camera snapshot is available for this session.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_camera_snapshot";
    tool_call.name = "camera_snapshot";
    tool_call.arguments = nlohmann::json::object();
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-camera-missing-tool";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-camera-missing-tool-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeMemoryWriteToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("\"ok\": true") !=
              std::string::npos) {
        response.content = "Mobile memory write completed.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_memory_write";
    tool_call.name = "memory_write";
    tool_call.arguments = {{"path", "notes/memory.md"},
                           {"content", "remember the dragonfruit"},
                           {"mode", "overwrite"}};
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-memory-write";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-memory-write-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeMemorySearchToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("dragonfruit project status") !=
              std::string::npos) {
        response.content = "I found the dragonfruit project note in memory.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_memory_search";
    tool_call.name = "memory_search";
    tool_call.arguments = {{"query", "dragonfruit"}, {"maxResults", 3}};
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-memory-search";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-memory-search-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeMemoryListToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("shopping.txt") !=
              std::string::npos &&
          last.content.front().content.find("project.md") !=
              std::string::npos) {
        response.content = "I found the mobile memory files.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_memory_list";
    tool_call.name = "memory_list";
    tool_call.arguments = {{"path", "notes"}, {"recursive", true}};
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-memory-list";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-memory-list-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeMemoryDeleteToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("\"removedCount\": 1") !=
              std::string::npos) {
        response.content = "Mobile memory delete completed.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_memory_delete";
    tool_call.name = "memory_delete";
    tool_call.arguments = {{"path", "notes/trash.md"}};
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-memory-delete";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-memory-delete-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeTimeToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("\"utc\"") != std::string::npos &&
          last.content.front().content.find("\"utcOffset\"") !=
              std::string::npos) {
        response.content = "Device time snapshot received.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_time";
    tool_call.name = "time";
    tool_call.arguments = nlohmann::json::object();
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-time-tool";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-time-tool-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeVibrateToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("\"durationMs\": 180") !=
              std::string::npos) {
        response.content = "Device haptic pulse triggered.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_vibrate";
    tool_call.name = "vibrate";
    tool_call.arguments = {{"durationMs", 180}};
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-vibrate-tool";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-vibrate-tool-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeRuntimeStatusToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("\"deviceBridgeAttached\"") !=
              std::string::npos &&
          last.content.front().content.find("\"webSearchReady\"") !=
              std::string::npos) {
        response.content = "Runtime readiness snapshot received.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_runtime_status";
    tool_call.name = "runtime_status";
    tool_call.arguments = nlohmann::json::object();
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-runtime-tool";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-runtime-tool-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeWebSearchToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("RavBot Android MVP") !=
              std::string::npos) {
        response.content = "I found the RavBot Android MVP search result.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_web_search";
    tool_call.name = "web_search";
    tool_call.arguments = {{"query", "ravbot android mvp"}, {"count", 3}};
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-web-search";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-web-search-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeWebFetchToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("Android embodied assistant") !=
              std::string::npos) {
        response.content = "I fetched the Android embodied assistant page.";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_web_fetch";
    tool_call.name = "web_fetch";
    tool_call.arguments = {
        {"url", "https://example.com/ravbot-android"},
        {"maxChars", 4096},
    };
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-web-fetch";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-web-fetch-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

std::vector<std::string>
tool_names(const ravbot::ChatCompletionRequest& request) {
  std::vector<std::string> names;
  for (const auto& tool : request.tools) {
    names.push_back(tool["function"].value("name", ""));
  }
  return names;
}

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

class FakeAsrProvider : public ravbot::mobile::MobileAsrProvider {
 public:
  struct SessionState {
    bool interrupted = false;
    bool has_pending_audio = false;
    uint64_t utterance_index = 0;
  };

  ravbot::mobile::AsrUpdate PushPcm16(const std::string& session_key,
                                      const int16_t* samples,
                                      size_t sample_count, int sample_rate_hz,
                                      bool end_of_turn) override {
    auto& state = session_states[session_key];
    (void)samples;
    (void)sample_count;
    if (state.interrupted) {
      state.interrupted = false;
      state.has_pending_audio = false;
      return {};
    }
    state.has_pending_audio = true;
    if (end_of_turn) {
      return FinalUpdate(state, sample_rate_hz, "vad_silence");
    }
    ravbot::mobile::AsrUpdate update;
    update.text = "ni hao";
    update.is_final = false;
    update.segment_index = state.utterance_index + 1;
    update.sample_rate_hz = 16000;
    update.duration_ms = 60;
    update.average_level = 0.1;
    update.peak_level = 0.2;
    update.end_reason = "streaming";
    return update;
  }

  ravbot::mobile::AsrUpdate Flush(const std::string& session_key) override {
    auto it = session_states.find(session_key);
    if (it == session_states.end()) {
      return {};
    }
    auto& state = it->second;
    if (state.interrupted) {
      state.interrupted = false;
      state.has_pending_audio = false;
      return {};
    }
    if (!state.has_pending_audio) {
      return {};
    }
    return FinalUpdate(state, 16000, "flush");
  }

  void Interrupt(const std::string& session_key) override {
    auto& state = session_states[session_key];
    state.interrupted = true;
    state.has_pending_audio = false;
    interrupted_sessions.push_back(session_key);
  }

  void ResetSession(const std::string& session_key) override {
    session_states.erase(session_key);
    reset_sessions.push_back(session_key);
  }

  std::vector<std::string> interrupted_sessions;
  std::vector<std::string> reset_sessions;
  std::unordered_map<std::string, SessionState> session_states;

 private:
  ravbot::mobile::AsrUpdate FinalUpdate(SessionState& state, int sample_rate_hz,
                                        const std::string& end_reason) {
    (void)sample_rate_hz;
    state.has_pending_audio = false;
    ravbot::mobile::AsrUpdate update;
    update.text = "ni hao ravbot";
    update.is_final = true;
    update.segment_index = state.utterance_index + 1;
    update.sample_rate_hz = 16000;
    update.duration_ms = end_reason == "flush" ? 180 : 120;
    update.average_level = end_reason == "flush" ? 0.14 : 0.12;
    update.peak_level = end_reason == "flush" ? 0.36 : 0.34;
    update.end_reason = end_reason;
    state.utterance_index += 1;
    return update;
  }
};

class FakeDeviceBridge : public ravbot::mobile::DeviceCapabilityBridge {
 public:
  std::string ResolveStateDirectory() const override {
    return "/tmp/state";
  }
  std::string ResolveModelsDirectory() const override {
    return "/tmp/models";
  }
  bool IsForeground() const override {
    return true;
  }
  bool SupportsWebSearch() const override {
    return true;
  }
  bool SupportsWebFetch() const override {
    return true;
  }
  bool SupportsVibration() const override {
    return true;
  }

  void SetAvatarState(const std::string& session_key,
                      ravbot::mobile::AvatarState state) override {
    avatar_states.push_back(session_key + ":" +
                            ravbot::mobile::AvatarStateToString(state));
  }

  void RequestSpeechPlayback(const std::string& session_key,
                             const std::string& text) override {
    speech_requests.push_back(session_key + ":" + text);
  }

  void InterruptSpeechPlayback(const std::string& session_key) override {
    last_interrupt_session = session_key;
    speech_interrupts += 1;
  }

  void Vibrate(const std::string& session_key, int duration_ms) override {
    vibration_calls.push_back(session_key + ":" + std::to_string(duration_ms));
  }

  std::string WebSearch(const std::string& session_key,
                        const std::string& query, int count,
                        const std::string& freshness) override {
    web_search_calls.push_back({session_key, query, count, freshness});
    return nlohmann::json{
        {"provider", "android_host"},
        {"sessionKey", session_key},
        {"query", query},
        {"freshness", freshness},
        {"results",
         nlohmann::json::array(
             {{{"title", "RavBot Android MVP"},
               {"url", "https://example.com/ravbot-android"},
               {"description", "Android embodied assistant planning page."}}})}}
        .dump(2);
  }

  std::string WebFetch(const std::string& session_key, const std::string& url,
                       int max_chars) override {
    web_fetch_calls.push_back({session_key, url, max_chars});
    return nlohmann::json{
        {"sessionKey", session_key},
        {"url", url},
        {"content", "Android embodied assistant page content."},
        {"contentType", "text/html"},
        {"maxChars", max_chars},
    }
        .dump(2);
  }

  std::vector<std::string> avatar_states;
  std::vector<std::string> speech_requests;
  int speech_interrupts = 0;
  std::string last_interrupt_session;
  std::vector<std::string> vibration_calls;
  std::vector<std::tuple<std::string, std::string, int, std::string>>
      web_search_calls;
  std::vector<std::tuple<std::string, std::string, int>> web_fetch_calls;
};

class FakeSpeechOnlyDeviceBridge
    : public ravbot::mobile::DeviceCapabilityBridge {
 public:
  std::string ResolveStateDirectory() const override {
    return "/tmp/state";
  }
  std::string ResolveModelsDirectory() const override {
    return "/tmp/models";
  }
  bool IsForeground() const override {
    return true;
  }
  bool SupportsWebSearch() const override {
    return false;
  }
  bool SupportsWebFetch() const override {
    return false;
  }

  void SetAvatarState(const std::string& /*session_key*/,
                      ravbot::mobile::AvatarState /*state*/) override {}
  void RequestSpeechPlayback(const std::string& /*session_key*/,
                             const std::string& /*text*/) override {}
  void InterruptSpeechPlayback(const std::string& /*session_key*/) override {}
  std::string WebSearch(const std::string& /*session_key*/,
                        const std::string& /*query*/, int /*count*/,
                        const std::string& /*freshness*/) override {
    return "{}";
  }
  std::string WebFetch(const std::string& /*session_key*/,
                       const std::string& /*url*/, int /*max_chars*/) override {
    return "{}";
  }
};

class FakeWebSearchOnlyDeviceBridge
    : public ravbot::mobile::DeviceCapabilityBridge {
 public:
  std::string ResolveStateDirectory() const override {
    return "/tmp/state";
  }
  std::string ResolveModelsDirectory() const override {
    return "/tmp/models";
  }
  bool IsForeground() const override {
    return true;
  }
  bool SupportsWebSearch() const override {
    return true;
  }
  bool SupportsWebFetch() const override {
    return false;
  }

  void SetAvatarState(const std::string& /*session_key*/,
                      ravbot::mobile::AvatarState /*state*/) override {}
  void RequestSpeechPlayback(const std::string& /*session_key*/,
                             const std::string& /*text*/) override {}
  void InterruptSpeechPlayback(const std::string& /*session_key*/) override {}
  std::string WebSearch(const std::string& /*session_key*/,
                        const std::string& /*query*/, int /*count*/,
                        const std::string& /*freshness*/) override {
    return "{}";
  }
  std::string WebFetch(const std::string& /*session_key*/,
                       const std::string& /*url*/, int /*max_chars*/) override {
    return "{}";
  }
};

class FakeWebFetchOnlyDeviceBridge
    : public ravbot::mobile::DeviceCapabilityBridge {
 public:
  std::string ResolveStateDirectory() const override {
    return "/tmp/state";
  }
  std::string ResolveModelsDirectory() const override {
    return "/tmp/models";
  }
  bool IsForeground() const override {
    return true;
  }
  bool SupportsWebSearch() const override {
    return false;
  }
  bool SupportsWebFetch() const override {
    return true;
  }

  void SetAvatarState(const std::string& /*session_key*/,
                      ravbot::mobile::AvatarState /*state*/) override {}
  void RequestSpeechPlayback(const std::string& /*session_key*/,
                             const std::string& /*text*/) override {}
  void InterruptSpeechPlayback(const std::string& /*session_key*/) override {}
  std::string WebSearch(const std::string& /*session_key*/,
                        const std::string& /*query*/, int /*count*/,
                        const std::string& /*freshness*/) override {
    return "{}";
  }
  std::string WebFetch(const std::string& /*session_key*/,
                       const std::string& /*url*/, int /*max_chars*/) override {
    return "{}";
  }
};

class FakeVisionProvider : public ravbot::mobile::MobileVisionProvider {
 public:
  std::optional<std::string>
  ObserveFrame(const ravbot::mobile::CameraFrame& frame) override {
    ++call_count;
    last_width = frame.width;
    return "desk with phone";
  }

  std::string ProviderName() const override {
    return "fake_vision_provider";
  }

  int call_count = 0;
  int last_width = 0;
};

class FakeUnavailableCameraReasonToolCallingTextProvider
    : public ravbot::LLMProvider {
 public:
  explicit FakeUnavailableCameraReasonToolCallingTextProvider(
      std::string expected_reason)
      : expected_reason_(std::move(expected_reason)) {}

  ravbot::ChatCompletionResponse
  ChatCompletion(const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("\"available\": false") !=
              std::string::npos &&
          last.content.front().content.find("\"reason\": \"" +
                                            expected_reason_ + "\"") !=
              std::string::npos) {
        response.content =
            "Camera snapshot unavailable because " + expected_reason_ + ".";
        return response;
      }
    }

    ravbot::ToolCall tool_call;
    tool_call.id = "tool_camera_snapshot";
    tool_call.name = "camera_snapshot";
    tool_call.arguments = nlohmann::json::object();
    response.tool_calls.push_back(tool_call);
    response.finish_reason = "tool_calls";
    return response;
  }

  void ChatCompletionStream(
      const ravbot::ChatCompletionRequest& request,
      std::function<void(const ravbot::ChatCompletionResponse&)> callback)
      override {
    requests.push_back(request);
    auto response = ChatCompletion(request);
    response.is_stream_end = true;
    callback(response);
  }

  std::string GetProviderName() const override {
    return "fake-camera-unavailable-tool";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-camera-unavailable-tool-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;

 private:
  std::string expected_reason_;
};

class MobileEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("mobile-engine-test", sink);
    test_dir_ =
        std::filesystem::temp_directory_path() / "ravbot_mobile_engine_test";
    std::filesystem::remove_all(test_dir_);
    std::filesystem::create_directories(test_dir_);
  }

  void TearDown() override {
    std::filesystem::remove_all(test_dir_);
  }

  ravbot::RavBotConfig MakeConfig() {
    ravbot::RavBotConfig config;
    config.mobile.runtime.enabled = true;
    config.mobile.runtime.foreground_only = true;
    config.mobile.vision.enabled = true;
    return config;
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::filesystem::path test_dir_;
};

TEST_F(MobileEngineTest, SendTextTurnPersistsReplyAndEmitsAssistantEvents) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeTextProvider>("hello from mobile");
  engine.SetTextProvider(provider);

  std::vector<std::string> names;
  engine.SubscribeEvents([&names](const ravbot::mobile::MobileEvent& event) {
    names.push_back(event.name);
  });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:mobile", "hi there"));

  auto history = engine.session_manager().GetHistory("agent:main:mobile");
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].role, "user");
  EXPECT_EQ(history[0].content[0].text, "hi there");
  EXPECT_EQ(history[1].role, "assistant");
  EXPECT_EQ(history[1].content[0].text, "hello from mobile");
  EXPECT_FALSE(provider->last_request_.messages.empty());
  EXPECT_TRUE(provider->last_request_.stream);
  EXPECT_NE(std::find(names.begin(), names.end(),
                      ravbot::mobile::kEventAssistantFinal),
            names.end());
  EXPECT_NE(std::find(names.begin(), names.end(),
                      ravbot::mobile::kEventMobileAvatarState),
            names.end());
}

TEST_F(MobileEngineTest, LocalRuntimeUsesDefaultLlamaCppStubProvider) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);

  ASSERT_TRUE(engine.SendTextTurn("agent:main:bootstrap", "hello"));

  auto history = engine.session_manager().GetHistory("agent:main:bootstrap");
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[1].role, "assistant");
  EXPECT_NE(history[1].content[0].text.find("llama.cpp"), std::string::npos);
}

TEST_F(MobileEngineTest, StartSessionUsesConfiguredAvatarDefaultState) {
  auto config = MakeConfig();
  config.mobile.avatar.default_state = "watch";
  ravbot::mobile::MobileEngine engine(config, test_dir_, test_dir_, logger_);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  EXPECT_EQ(engine.StartSession("agent:main:avatar"), "agent:main:avatar");
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.back().name, ravbot::mobile::kEventMobileAvatarState);
  EXPECT_EQ(events.back().payload["state"], "watch");
}

TEST_F(MobileEngineTest, ReportDeviceStatusEmitsSnapshotAndTracksForeground) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ravbot::mobile::DeviceStatusSnapshot status;
  status.service_running = true;
  status.capture_requested = true;
  status.permissions_granted = true;
  status.host_web_search_enabled = false;
  status.host_web_fetch_enabled = true;
  status.host_haptics_enabled = false;
  status.microphone_status = "running";
  status.camera_status = "running";
  status.speaker_status = "speaking";
  status.timestamp_ms = 42;

  ASSERT_TRUE(engine.ReportDeviceStatus("agent:main:device-status", status));
  auto first_status = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileDeviceStatus;
      });
  ASSERT_NE(first_status, events.end());
  EXPECT_EQ(first_status->payload["sessionKey"], "agent:main:device-status");
  EXPECT_TRUE(first_status->payload["foreground"]);
  EXPECT_TRUE(first_status->payload["serviceRunning"]);
  EXPECT_FALSE(first_status->payload["hostWebSearchEnabled"]);
  EXPECT_TRUE(first_status->payload["hostWebFetchEnabled"]);
  EXPECT_FALSE(first_status->payload["hostHapticsEnabled"]);
  EXPECT_EQ(first_status->payload["microphoneStatus"], "running");
  EXPECT_EQ(first_status->payload["speakerStatus"], "speaking");
  EXPECT_TRUE(first_status->payload.contains("hostCapabilities"));
  EXPECT_TRUE(first_status->payload["hostCapabilities"]["capture"]["ready"]);
  EXPECT_TRUE(first_status->payload["hostCapabilities"]["microphone"]["ready"]);
  EXPECT_TRUE(first_status->payload["hostCapabilities"]["camera"]["ready"]);
  EXPECT_EQ(first_status->payload["hostCapabilities"]["webSearch"]["reason"],
            "device_bridge_missing");
  EXPECT_EQ(first_status->payload["hostCapabilities"]["haptics"]["reason"],
            "device_bridge_missing");

  engine.SetForegroundState(false);
  auto latest_status = std::find_if(
      events.rbegin(), events.rend(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileDeviceStatus;
      });
  ASSERT_NE(latest_status, events.rend());
  EXPECT_EQ(latest_status->payload["sessionKey"], "agent:main:device-status");
  EXPECT_FALSE(latest_status->payload["foreground"]);
  EXPECT_TRUE(latest_status->payload["serviceRunning"]);
  EXPECT_FALSE(latest_status->payload["hostWebSearchEnabled"]);
  EXPECT_TRUE(latest_status->payload["hostWebFetchEnabled"]);
  EXPECT_FALSE(latest_status->payload["hostHapticsEnabled"]);
  EXPECT_EQ(latest_status->payload["hostCapabilities"]["capture"]["reason"],
            "background_gated");
  EXPECT_EQ(latest_status->payload["hostCapabilities"]["camera"]["reason"],
            "background_gated");
}

TEST_F(MobileEngineTest, PushPcm16UsesAsrProviderForFinalTurn) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  engine.SetTextProvider(std::make_shared<FakeTextProvider>("speech reply"));
  engine.SetAsrProvider(std::make_shared<FakeAsrProvider>());

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  int16_t samples[4] = {1, 2, 3, 4};
  ASSERT_TRUE(engine.PushPcm16("agent:main:speech", samples, 4, 16000, true));

  auto history = engine.session_manager().GetHistory("agent:main:speech");
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].content[0].text, "ni hao ravbot");
  EXPECT_EQ(history[1].content[0].text, "speech reply");
  auto asr_final =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventMobileAsrFinal;
                   });
  ASSERT_NE(asr_final, events.end());
  EXPECT_EQ(asr_final->payload["sessionKey"], "agent:main:speech");
  EXPECT_EQ(asr_final->payload["segmentIndex"], 1);
  EXPECT_EQ(asr_final->payload["durationMs"], 120);
  EXPECT_EQ(asr_final->payload["sampleRateHz"], 16000);
  EXPECT_EQ(asr_final->payload["endReason"], "vad_silence");
}

TEST_F(MobileEngineTest, FlushAudioTurnFinalizesBufferedSpeechTurn) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  engine.SetTextProvider(
      std::make_shared<FakeTextProvider>("speech reply after flush"));
  engine.SetAsrProvider(std::make_shared<FakeAsrProvider>());

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  int16_t samples[4] = {1, 2, 3, 4};
  ASSERT_TRUE(
      engine.PushPcm16("agent:main:speech-flush", samples, 4, 16000, false));
  ASSERT_TRUE(engine.FlushAudioTurn("agent:main:speech-flush"));

  auto history = engine.session_manager().GetHistory("agent:main:speech-flush");
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].content[0].text, "ni hao ravbot");
  EXPECT_EQ(history[1].content[0].text, "speech reply after flush");
  auto asr_partial = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileAsrPartial;
      });
  ASSERT_NE(asr_partial, events.end());
  EXPECT_EQ(asr_partial->payload["sessionKey"], "agent:main:speech-flush");
  EXPECT_EQ(asr_partial->payload["endReason"], "streaming");

  auto asr_final =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventMobileAsrFinal;
                   });
  ASSERT_NE(asr_final, events.end());
  EXPECT_EQ(asr_final->payload["sessionKey"], "agent:main:speech-flush");
  EXPECT_EQ(asr_final->payload["segmentIndex"], 1);
  EXPECT_EQ(asr_final->payload["durationMs"], 180);
  EXPECT_EQ(asr_final->payload["endReason"], "flush");
}

TEST_F(MobileEngineTest, FlushAudioTurnAndInterruptStayScopedPerSession) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  engine.SetTextProvider(
      std::make_shared<FakeTextProvider>("speech reply after isolation"));
  auto asr_provider = std::make_shared<FakeAsrProvider>();
  engine.SetAsrProvider(asr_provider);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  int16_t samples[4] = {1, 2, 3, 4};
  ASSERT_TRUE(
      engine.PushPcm16("agent:main:speech-a", samples, 4, 16000, false));
  ASSERT_TRUE(
      engine.PushPcm16("agent:main:speech-b", samples, 4, 16000, false));

  engine.InterruptGeneration("agent:main:speech-b");
  ASSERT_TRUE(engine.FlushAudioTurn("agent:main:speech-a"));
  EXPECT_FALSE(engine.FlushAudioTurn("agent:main:speech-b"));

  auto history_a = engine.session_manager().GetHistory("agent:main:speech-a");
  ASSERT_EQ(history_a.size(), 2u);
  EXPECT_EQ(history_a[0].content[0].text, "ni hao ravbot");
  EXPECT_EQ(history_a[1].content[0].text, "speech reply after isolation");

  auto history_b = engine.session_manager().GetHistory("agent:main:speech-b");
  EXPECT_TRUE(history_b.empty());
  ASSERT_EQ(asr_provider->interrupted_sessions.size(), 1u);
  EXPECT_EQ(asr_provider->interrupted_sessions[0], "agent:main:speech-b");

  size_t final_event_count = 0;
  bool saw_a_final = false;
  bool saw_b_final = false;
  for (const auto& event : events) {
    if (event.name != ravbot::mobile::kEventMobileAsrFinal) {
      continue;
    }
    final_event_count += 1;
    if (event.payload.value("sessionKey", "") == "agent:main:speech-a") {
      saw_a_final = true;
      EXPECT_EQ(event.payload["endReason"], "flush");
    }
    if (event.payload.value("sessionKey", "") == "agent:main:speech-b") {
      saw_b_final = true;
    }
  }
  EXPECT_EQ(final_event_count, 1u);
  EXPECT_TRUE(saw_a_final);
  EXPECT_FALSE(saw_b_final);
}

TEST_F(MobileEngineTest, StartSessionClearsPendingAudioForSameSession) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  engine.SetTextProvider(
      std::make_shared<FakeTextProvider>("speech reply after reset"));
  auto asr_provider = std::make_shared<FakeAsrProvider>();
  engine.SetAsrProvider(asr_provider);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  int16_t samples[4] = {1, 2, 3, 4};
  ASSERT_TRUE(
      engine.PushPcm16("agent:main:speech-reset", samples, 4, 16000, false));

  ASSERT_EQ(engine.StartSession("agent:main:speech-reset"),
            "agent:main:speech-reset");
  EXPECT_FALSE(engine.FlushAudioTurn("agent:main:speech-reset"));
  ASSERT_TRUE(
      engine.PushPcm16("agent:main:speech-reset", samples, 4, 16000, true));

  ASSERT_EQ(asr_provider->reset_sessions.size(), 1u);
  EXPECT_EQ(asr_provider->reset_sessions[0], "agent:main:speech-reset");

  auto asr_final = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileAsrFinal &&
               event.payload.value("sessionKey", "") ==
                   "agent:main:speech-reset";
      });
  ASSERT_NE(asr_final, events.end());
  EXPECT_EQ(asr_final->payload["segmentIndex"], 1);
  EXPECT_EQ(asr_final->payload["endReason"], "vad_silence");
}

TEST_F(MobileEngineTest, PushCameraFrameRequiresForegroundWhenConfigured) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto vision = std::make_shared<FakeVisionProvider>();
  engine.SetVisionProvider(vision);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ravbot::mobile::CameraFrame frame;
  frame.width = 320;
  frame.height = 240;
  frame.timestamp_ms = 1234;
  frame.data = {0, 1, 2, 3};

  engine.SetForegroundState(false);
  EXPECT_FALSE(engine.PushCameraFrame("agent:main:vision", frame));

  engine.SetForegroundState(true);
  ASSERT_TRUE(engine.PushCameraFrame("agent:main:vision", frame));
  ASSERT_FALSE(events.empty());
  auto it = std::find_if(events.begin(), events.end(),
                         [](const ravbot::mobile::MobileEvent& event) {
                           return event.name ==
                                  ravbot::mobile::kEventMobileVisionObservation;
                         });
  ASSERT_NE(it, events.end());
  EXPECT_EQ(it->payload["sessionKey"], "agent:main:vision");
  EXPECT_EQ(vision->last_width, 320);
}

TEST_F(MobileEngineTest, PushCameraFrameSkipsFramesInsideSampleInterval) {
  auto config = MakeConfig();
  config.mobile.vision.sample_fps = 1.0;
  config.mobile.vision.scene_change_threshold = 0.0;
  ravbot::mobile::MobileEngine engine(config, test_dir_, test_dir_, logger_);
  auto vision = std::make_shared<FakeVisionProvider>();
  engine.SetVisionProvider(vision);

  ravbot::mobile::CameraFrame first;
  first.width = 320;
  first.height = 240;
  first.format = "YUV420_LUMA";
  first.timestamp_ms = 1000;
  first.data.assign(320 * 240, static_cast<uint8_t>(32));

  ravbot::mobile::CameraFrame second = first;
  second.timestamp_ms = 1500;
  second.data.assign(320 * 240, static_cast<uint8_t>(240));

  ASSERT_TRUE(engine.PushCameraFrame("agent:main:vision-throttle", first));
  ASSERT_TRUE(engine.PushCameraFrame("agent:main:vision-throttle", second));
  EXPECT_EQ(vision->call_count, 1);
}

TEST_F(MobileEngineTest, PushCameraFrameSkipsSmallSceneChanges) {
  auto config = MakeConfig();
  config.mobile.vision.sample_fps = 10.0;
  config.mobile.vision.scene_change_threshold = 0.2;
  ravbot::mobile::MobileEngine engine(config, test_dir_, test_dir_, logger_);
  auto vision = std::make_shared<FakeVisionProvider>();
  engine.SetVisionProvider(vision);

  ravbot::mobile::CameraFrame first;
  first.width = 64;
  first.height = 64;
  first.format = "YUV420_LUMA";
  first.timestamp_ms = 1000;
  first.data.assign(64 * 64, static_cast<uint8_t>(32));

  ravbot::mobile::CameraFrame second = first;
  second.timestamp_ms = 2000;
  second.data.assign(64 * 64, static_cast<uint8_t>(48));

  ravbot::mobile::CameraFrame third = first;
  third.timestamp_ms = 3000;
  third.data.assign(64 * 64, static_cast<uint8_t>(220));

  ASSERT_TRUE(engine.PushCameraFrame("agent:main:vision-scene", first));
  ASSERT_TRUE(engine.PushCameraFrame("agent:main:vision-scene", second));
  ASSERT_TRUE(engine.PushCameraFrame("agent:main:vision-scene", third));
  EXPECT_EQ(vision->call_count, 2);
}

TEST_F(MobileEngineTest, PushCameraFrameSamplingStateIsScopedPerSession) {
  auto config = MakeConfig();
  config.mobile.vision.sample_fps = 1.0;
  config.mobile.vision.scene_change_threshold = 0.5;
  ravbot::mobile::MobileEngine engine(config, test_dir_, test_dir_, logger_);
  auto vision = std::make_shared<FakeVisionProvider>();
  engine.SetVisionProvider(vision);

  ravbot::mobile::CameraFrame frame_a;
  frame_a.width = 64;
  frame_a.height = 64;
  frame_a.format = "YUV420_LUMA";
  frame_a.timestamp_ms = 1000;
  frame_a.data.assign(64 * 64, static_cast<uint8_t>(32));

  ravbot::mobile::CameraFrame frame_b = frame_a;
  frame_b.timestamp_ms = 1500;

  ASSERT_TRUE(engine.PushCameraFrame("agent:main:vision-session-a", frame_a));
  ASSERT_TRUE(engine.PushCameraFrame("agent:main:vision-session-b", frame_b));
  EXPECT_EQ(vision->call_count, 2);
}

TEST_F(MobileEngineTest, StartSessionEmitsRuntimeStatusWithResolvedModelPaths) {
  auto config = MakeConfig();
  config.mobile.models.llm_model = "Qwen3.5-0.8B-Q4_K_M.gguf";
  config.mobile.models.vlm_model = "SmolVLM-500M-Instruct-Q8_0.gguf";
  config.mobile.models.mmproj_model = "mmproj-SmolVLM-500M-Instruct-Q8_0.gguf";
  ravbot::mobile::MobileEngine engine(config, test_dir_, test_dir_, logger_);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_EQ(engine.StartSession("agent:main:runtime-status"),
            "agent:main:runtime-status");

  auto it = std::find_if(events.begin(), events.end(),
                         [](const ravbot::mobile::MobileEvent& event) {
                           return event.name ==
                                  ravbot::mobile::kEventMobileRuntimeStatus;
                         });
  ASSERT_NE(it, events.end());
  EXPECT_EQ(it->payload["provider"], "llama_cpp_mobile");
  EXPECT_EQ(it->payload["speechProvider"], "sherpa_onnx_mobile");
  EXPECT_EQ(it->payload["modelsDir"], test_dir_.string());
  EXPECT_EQ(it->payload["llmModelPath"],
            (test_dir_ / "Qwen3.5-0.8B-Q4_K_M.gguf").string());
  EXPECT_EQ(it->payload["sttModelPath"],
            (test_dir_ / "SenseVoiceSmall").string());
  EXPECT_FALSE(it->payload["llmModelExists"].get<bool>());
  EXPECT_FALSE(it->payload["sttModelExists"].get<bool>());
  EXPECT_FALSE(it->payload["visionProviderReady"].get<bool>());
  EXPECT_EQ(it->payload["visionProvider"], "placeholder_mobile_vision");
  EXPECT_TRUE(it->payload["visionProviderPlaceholder"].get<bool>());
  EXPECT_EQ(it->payload["visionDetail"],
            "Using placeholder mobile vision provider until a real VLM "
            "backend is linked.");
  EXPECT_FALSE(it->payload["deviceBridgeAttached"].get<bool>());
  EXPECT_FALSE(it->payload["webSearchReady"].get<bool>());
  EXPECT_FALSE(it->payload["webFetchReady"].get<bool>());
  EXPECT_FALSE(it->payload["vibrationReady"].get<bool>());
  EXPECT_TRUE(json_array_contains_string(it->payload["availableTools"],
                                         "device_status"));
  EXPECT_TRUE(json_array_contains_string(it->payload["availableTools"],
                                         "runtime_status"));
  EXPECT_TRUE(json_array_contains_string(it->payload["availableTools"],
                                         "camera_snapshot"));
  EXPECT_TRUE(
      json_array_contains_string(it->payload["availableTools"], "memory_list"));
  EXPECT_FALSE(
      json_array_contains_string(it->payload["availableTools"], "web_search"));
  EXPECT_FALSE(
      json_array_contains_string(it->payload["availableTools"], "web_fetch"));
  EXPECT_FALSE(
      json_array_contains_string(it->payload["availableTools"], "vibrate"));
  EXPECT_EQ(it->payload["toolAvailability"]["web_search"]["available"], false);
  EXPECT_EQ(it->payload["toolAvailability"]["web_search"]["reason"],
            "device_bridge_missing");
  EXPECT_EQ(it->payload["toolAvailability"]["web_fetch"]["reason"],
            "device_bridge_missing");
  EXPECT_EQ(it->payload["toolAvailability"]["vibrate"]["reason"],
            "device_bridge_missing");
}

TEST_F(MobileEngineTest, RuntimeStatusReflectsDeviceBridgeWebCapabilities) {
  ravbot::mobile::MobileEngine speech_only_engine(MakeConfig(), test_dir_,
                                                  test_dir_, logger_);
  speech_only_engine.SetDeviceBridge(
      std::make_shared<FakeSpeechOnlyDeviceBridge>());

  std::vector<ravbot::mobile::MobileEvent> speech_only_events;
  speech_only_engine.SubscribeEvents(
      [&speech_only_events](const ravbot::mobile::MobileEvent& event) {
        speech_only_events.push_back(event);
      });

  ASSERT_EQ(speech_only_engine.StartSession("agent:main:runtime-speech-only"),
            "agent:main:runtime-speech-only");

  auto speech_only_status = std::find_if(
      speech_only_events.begin(), speech_only_events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileRuntimeStatus;
      });
  ASSERT_NE(speech_only_status, speech_only_events.end());
  EXPECT_FALSE(speech_only_status->payload["visionProviderReady"].get<bool>());
  EXPECT_EQ(speech_only_status->payload["visionProvider"],
            "placeholder_mobile_vision");
  EXPECT_TRUE(
      speech_only_status->payload["visionProviderPlaceholder"].get<bool>());
  EXPECT_TRUE(speech_only_status->payload["deviceBridgeAttached"].get<bool>());
  EXPECT_FALSE(speech_only_status->payload["webSearchReady"].get<bool>());
  EXPECT_FALSE(speech_only_status->payload["webFetchReady"].get<bool>());
  EXPECT_FALSE(speech_only_status->payload["vibrationReady"].get<bool>());
  EXPECT_FALSE(json_array_contains_string(
      speech_only_status->payload["availableTools"], "web_search"));
  EXPECT_FALSE(json_array_contains_string(
      speech_only_status->payload["availableTools"], "web_fetch"));
  EXPECT_FALSE(json_array_contains_string(
      speech_only_status->payload["availableTools"], "vibrate"));
  EXPECT_EQ(
      speech_only_status->payload["toolAvailability"]["web_search"]["reason"],
      "web_search_unsupported");
  EXPECT_EQ(
      speech_only_status->payload["toolAvailability"]["web_fetch"]["reason"],
      "web_fetch_unsupported");
  EXPECT_EQ(
      speech_only_status->payload["toolAvailability"]["vibrate"]["reason"],
      "vibration_unsupported");

  ravbot::mobile::MobileEngine full_bridge_engine(MakeConfig(), test_dir_,
                                                  test_dir_, logger_);
  full_bridge_engine.SetDeviceBridge(std::make_shared<FakeDeviceBridge>());

  std::vector<ravbot::mobile::MobileEvent> full_bridge_events;
  full_bridge_engine.SubscribeEvents(
      [&full_bridge_events](const ravbot::mobile::MobileEvent& event) {
        full_bridge_events.push_back(event);
      });

  ASSERT_EQ(full_bridge_engine.StartSession("agent:main:runtime-web-ready"),
            "agent:main:runtime-web-ready");

  auto full_bridge_status = std::find_if(
      full_bridge_events.begin(), full_bridge_events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileRuntimeStatus;
      });
  ASSERT_NE(full_bridge_status, full_bridge_events.end());
  EXPECT_FALSE(full_bridge_status->payload["visionProviderReady"].get<bool>());
  EXPECT_EQ(full_bridge_status->payload["visionProvider"],
            "placeholder_mobile_vision");
  EXPECT_TRUE(
      full_bridge_status->payload["visionProviderPlaceholder"].get<bool>());
  EXPECT_TRUE(full_bridge_status->payload["deviceBridgeAttached"].get<bool>());
  EXPECT_TRUE(full_bridge_status->payload["webSearchReady"].get<bool>());
  EXPECT_TRUE(full_bridge_status->payload["webFetchReady"].get<bool>());
  EXPECT_TRUE(full_bridge_status->payload["vibrationReady"].get<bool>());
  EXPECT_TRUE(json_array_contains_string(
      full_bridge_status->payload["availableTools"], "web_search"));
  EXPECT_TRUE(json_array_contains_string(
      full_bridge_status->payload["availableTools"], "web_fetch"));
  EXPECT_TRUE(json_array_contains_string(
      full_bridge_status->payload["availableTools"], "vibrate"));
  EXPECT_EQ(full_bridge_status
                ->payload["toolAvailability"]["web_search"]["available"],
            true);
  EXPECT_EQ(
      full_bridge_status->payload["toolAvailability"]["web_fetch"]["available"],
      true);
  EXPECT_EQ(
      full_bridge_status->payload["toolAvailability"]["vibrate"]["available"],
      true);
}

TEST_F(MobileEngineTest, SetDeviceBridgeEmitsRuntimeStatusUpdate) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  engine.SetDeviceBridge(std::make_shared<FakeSpeechOnlyDeviceBridge>());
  engine.SetDeviceBridge(std::make_shared<FakeDeviceBridge>());

  std::vector<nlohmann::json> runtime_payloads;
  for (const auto& event : events) {
    if (event.name == ravbot::mobile::kEventMobileRuntimeStatus) {
      runtime_payloads.push_back(event.payload);
    }
  }

  ASSERT_GE(runtime_payloads.size(), 3u);
  EXPECT_FALSE(runtime_payloads[0]["deviceBridgeAttached"].get<bool>());
  EXPECT_FALSE(runtime_payloads[0]["webSearchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[0]["webFetchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[0]["vibrationReady"].get<bool>());
  EXPECT_EQ(runtime_payloads[0]["toolAvailability"]["web_search"]["reason"],
            "device_bridge_missing");

  EXPECT_TRUE(runtime_payloads[1]["deviceBridgeAttached"].get<bool>());
  EXPECT_FALSE(runtime_payloads[1]["webSearchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[1]["webFetchReady"].get<bool>());
  EXPECT_FALSE(runtime_payloads[1]["vibrationReady"].get<bool>());
  EXPECT_EQ(runtime_payloads[1]["toolAvailability"]["web_search"]["reason"],
            "web_search_unsupported");

  EXPECT_TRUE(runtime_payloads[2]["deviceBridgeAttached"].get<bool>());
  EXPECT_TRUE(runtime_payloads[2]["webSearchReady"].get<bool>());
  EXPECT_TRUE(runtime_payloads[2]["webFetchReady"].get<bool>());
  EXPECT_TRUE(runtime_payloads[2]["vibrationReady"].get<bool>());
  EXPECT_TRUE(json_array_contains_string(runtime_payloads[2]["availableTools"],
                                         "web_search"));
  EXPECT_TRUE(json_array_contains_string(runtime_payloads[2]["availableTools"],
                                         "web_fetch"));
  EXPECT_TRUE(json_array_contains_string(runtime_payloads[2]["availableTools"],
                                         "vibrate"));
}

TEST_F(MobileEngineTest, ReportTtsPlaybackStateEmitsEventAndAvatarState) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(engine.ReportTtsPlaybackState("agent:main:tts", "speaking"));
  ASSERT_TRUE(engine.ReportTtsPlaybackState("agent:main:tts", "completed"));

  auto speaking = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileTtsState &&
               event.payload.value("state", "") == "speaking";
      });
  ASSERT_NE(speaking, events.end());
  EXPECT_EQ(speaking->payload["sessionKey"], "agent:main:tts");

  auto avatar_idle = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileAvatarState &&
               event.payload.value("state", "") == "idle";
      });
  EXPECT_NE(avatar_idle, events.end());
}

TEST_F(MobileEngineTest, SendTextTurnStreamsAssistantDeltaChunks) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeStreamingTextProvider>();
  engine.SetTextProvider(provider);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:stream", "stream please"));

  std::vector<std::string> delta_texts;
  for (const auto& event : events) {
    if (event.name == ravbot::mobile::kEventAssistantDelta) {
      delta_texts.push_back(event.payload.value("text", ""));
    }
  }

  ASSERT_EQ(delta_texts.size(), 2u);
  EXPECT_EQ(delta_texts[0], "hello ");
  EXPECT_EQ(delta_texts[1], "stream");
  for (const auto& event : events) {
    if (event.name == ravbot::mobile::kEventAssistantDelta) {
      EXPECT_EQ(event.payload["sessionKey"], "agent:main:stream");
    }
  }

  auto history = engine.session_manager().GetHistory("agent:main:stream");
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[1].content[0].text, "hello stream");
  EXPECT_TRUE(provider->last_request_.stream);
}

TEST_F(MobileEngineTest, SendTextTurnExecutesDeviceStatusToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  ravbot::mobile::DeviceStatusSnapshot status;
  status.service_running = true;
  status.capture_requested = true;
  status.permissions_granted = true;
  status.host_web_search_enabled = false;
  status.host_web_fetch_enabled = true;
  status.host_haptics_enabled = false;
  status.microphone_status = "running";
  status.camera_status = "running";
  status.speaker_status = "speaking";
  ASSERT_TRUE(engine.ReportDeviceStatus("agent:main:tooling", status));

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:tooling", "How is the device?"));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "device_status"),
            names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "camera_snapshot"),
            names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "memory_list"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "memory_search"),
            names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "memory_delete"),
            names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "time"), names.end());

  auto history = engine.session_manager().GetHistory("agent:main:tooling");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[0].role, "user");
  ASSERT_EQ(history[1].content.size(), 1u);
  EXPECT_EQ(history[1].role, "assistant");
  EXPECT_EQ(history[1].content[0].type, "tool_use");
  EXPECT_EQ(history[1].content[0].name, "device_status");
  EXPECT_EQ(history[2].role, "user");
  ASSERT_EQ(history[2].content.size(), 1u);
  EXPECT_EQ(history[2].content[0].type, "tool_result");
  EXPECT_NE(history[2].content[0].content.find("\"serviceRunning\": true"),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"runtimeStatus\""),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"provider\": \"fake-tool\""),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"webSearchReady\": false"),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"webFetchReady\": false"),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"vibrationReady\": false"),
            std::string::npos);
  EXPECT_NE(
      history[2].content[0].content.find("\"hostWebSearchEnabled\": false"),
      std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"hostWebFetchEnabled\": true"),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"hostHapticsEnabled\": false"),
            std::string::npos);
  const auto device_tool_result =
      nlohmann::json::parse(history[2].content[0].content);
  EXPECT_TRUE(device_tool_result.contains("hostCapabilities"));
  EXPECT_TRUE(device_tool_result["hostCapabilities"]["capture"]["ready"]);
  EXPECT_TRUE(device_tool_result["hostCapabilities"]["microphone"]["ready"]);
  EXPECT_TRUE(device_tool_result["hostCapabilities"]["camera"]["ready"]);
  EXPECT_EQ(device_tool_result["hostCapabilities"]["webSearch"]["reason"],
            "device_bridge_missing");
  EXPECT_EQ(device_tool_result["hostCapabilities"]["webFetch"]["reason"],
            "device_bridge_missing");
  EXPECT_EQ(device_tool_result["hostCapabilities"]["haptics"]["reason"],
            "device_bridge_missing");
  EXPECT_EQ(history[3].role, "assistant");
  EXPECT_EQ(history[3].content[0].text,
            "Device status received. Service is running and speaker is "
            "speaking.");

  auto tool_start =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolStart;
                   });
  ASSERT_NE(tool_start, events.end());
  EXPECT_EQ(tool_start->payload["sessionKey"], "agent:main:tooling");
  EXPECT_EQ(tool_start->payload["name"], "device_status");

  auto tool_result =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolResult;
                   });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["sessionKey"], "agent:main:tooling");
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"runtimeStatus\""),
            std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"provider\": \"fake-tool\""),
            std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"hostWebSearchEnabled\": false"),
            std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"hostHapticsEnabled\": false"),
            std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"speakerStatus\": \"speaking\""),
            std::string::npos);
  const auto device_event_result =
      nlohmann::json::parse(tool_result->payload["result"].get<std::string>());
  EXPECT_TRUE(device_event_result["hostCapabilities"]["capture"]["ready"]);
  EXPECT_EQ(device_event_result["hostCapabilities"]["webSearch"]["reason"],
            "device_bridge_missing");
  EXPECT_EQ(device_event_result["hostCapabilities"]["haptics"]["reason"],
            "device_bridge_missing");

  auto assistant_final =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventAssistantFinal;
                   });
  ASSERT_NE(assistant_final, events.end());
  EXPECT_EQ(assistant_final->payload["sessionKey"], "agent:main:tooling");
  EXPECT_EQ(assistant_final->payload["finishReason"], "stop");
}

TEST_F(MobileEngineTest,
       DeviceStatusCapabilitiesRespectHostTogglesWhenBridgePresent) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  engine.SetDeviceBridge(std::make_shared<FakeDeviceBridge>());
  auto provider = std::make_shared<FakeToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  ravbot::mobile::DeviceStatusSnapshot status;
  status.service_running = true;
  status.capture_requested = true;
  status.permissions_granted = true;
  status.host_web_search_enabled = false;
  status.host_web_fetch_enabled = true;
  status.host_haptics_enabled = false;
  status.microphone_status = "running";
  status.camera_status = "running";
  status.speaker_status = "idle";
  ASSERT_TRUE(
      engine.ReportDeviceStatus("agent:main:tooling-host-toggles", status));

  ASSERT_TRUE(engine.SendTextTurn("agent:main:tooling-host-toggles",
                                  "How is the device right now?"));

  auto history =
      engine.session_manager().GetHistory("agent:main:tooling-host-toggles");
  ASSERT_EQ(history.size(), 4u);
  const auto device_tool_result =
      nlohmann::json::parse(history[2].content[0].content);
  EXPECT_EQ(device_tool_result["hostCapabilities"]["webSearch"]["reason"],
            "host_toggle_off");
  EXPECT_FALSE(device_tool_result["hostCapabilities"]["webSearch"]["ready"]);
  EXPECT_TRUE(device_tool_result["hostCapabilities"]["webFetch"]["ready"]);
  EXPECT_EQ(device_tool_result["hostCapabilities"]["haptics"]["reason"],
            "host_toggle_off");
  EXPECT_FALSE(device_tool_result["hostCapabilities"]["haptics"]["ready"]);
}

TEST_F(MobileEngineTest, DeviceStatusToolRoundTripUsesCurrentSessionSnapshot) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  ravbot::mobile::DeviceStatusSnapshot session_a_status;
  session_a_status.service_running = true;
  session_a_status.capture_requested = true;
  session_a_status.permissions_granted = true;
  session_a_status.microphone_status = "running";
  session_a_status.camera_status = "running";
  session_a_status.speaker_status = "idle";
  ASSERT_TRUE(
      engine.ReportDeviceStatus("agent:main:device-a", session_a_status));

  ravbot::mobile::DeviceStatusSnapshot session_b_status;
  session_b_status.service_running = true;
  session_b_status.capture_requested = true;
  session_b_status.permissions_granted = true;
  session_b_status.microphone_status = "running";
  session_b_status.camera_status = "running";
  session_b_status.speaker_status = "speaking";
  ASSERT_TRUE(
      engine.ReportDeviceStatus("agent:main:device-b", session_b_status));

  ASSERT_TRUE(engine.SendTextTurn("agent:main:device-a",
                                  "How is the device right now?"));

  auto history = engine.session_manager().GetHistory("agent:main:device-a");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "device_status");
  EXPECT_NE(history[2].content[0].content.find("\"speakerStatus\": \"idle\""),
            std::string::npos);
  EXPECT_EQ(
      history[2].content[0].content.find("\"speakerStatus\": \"speaking\""),
      std::string::npos);
}

TEST_F(MobileEngineTest, SendTextTurnExecutesCameraSnapshotToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeCameraToolCallingTextProvider>();
  auto vision = std::make_shared<FakeVisionProvider>();
  engine.SetTextProvider(provider);
  engine.SetVisionProvider(vision);
  ravbot::mobile::DeviceStatusSnapshot status;
  status.service_running = true;
  status.capture_requested = true;
  status.permissions_granted = true;
  status.camera_status = "running";
  status.microphone_status = "running";
  status.speaker_status = "idle";
  ASSERT_TRUE(engine.ReportDeviceStatus("agent:main:camera-tool", status));

  ravbot::mobile::CameraFrame frame;
  frame.width = 320;
  frame.height = 240;
  frame.format = "YUV420_LUMA";
  frame.timestamp_ms = 4242;
  frame.data.assign(320 * 240, static_cast<uint8_t>(128));
  ASSERT_TRUE(engine.PushCameraFrame("agent:main:camera-tool", frame));

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(
      engine.SendTextTurn("agent:main:camera-tool", "What do you see now?"));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "camera_snapshot"),
            names.end());

  auto history = engine.session_manager().GetHistory("agent:main:camera-tool");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[0].role, "user");
  EXPECT_EQ(history[1].role, "assistant");
  EXPECT_EQ(history[1].content[0].type, "tool_use");
  EXPECT_EQ(history[1].content[0].name, "camera_snapshot");
  EXPECT_EQ(history[2].role, "user");
  EXPECT_EQ(history[2].content[0].type, "tool_result");
  EXPECT_NE(
      history[2].content[0].content.find("\"summary\": \"desk with phone\""),
      std::string::npos);
  EXPECT_NE(
      history[2].content[0].content.find("\"capturedWhileForeground\": true"),
      std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"stale\": true"),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"ageMs\":"),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find(
                "\"visionProvider\": \"fake_vision_provider\""),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find(
                "\"visionProviderPlaceholder\": false"),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"cameraStatus\": \"running\""),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"captureRequested\": true"),
            std::string::npos);
  const auto camera_tool_result =
      nlohmann::json::parse(history[2].content[0].content);
  EXPECT_TRUE(camera_tool_result["hostCapabilities"]["capture"]["ready"]);
  EXPECT_TRUE(camera_tool_result["hostCapabilities"]["camera"]["ready"]);
  EXPECT_EQ(camera_tool_result["hostCapabilities"]["webSearch"]["reason"],
            "device_bridge_missing");
  EXPECT_EQ(history[3].role, "assistant");
  EXPECT_EQ(history[3].content[0].text,
            "Latest camera observation shows a desk with phone.");

  auto tool_result = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventToolResult &&
               event.payload.value("name", "") == "camera_snapshot";
      });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["sessionKey"], "agent:main:camera-tool");
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"timestampMs\": 4242"),
            std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"capturedWhileForeground\": true"),
            std::string::npos);
  EXPECT_NE(
      tool_result->payload["result"].get<std::string>().find("\"stale\": true"),
      std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"visionProvider\": \"fake_vision_provider\""),
            std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"visionProviderPlaceholder\": false"),
            std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"deviceStatusAvailable\": true"),
            std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"cameraStatus\": \"running\""),
            std::string::npos);
}

TEST_F(MobileEngineTest, CameraSnapshotStaysScopedToCurrentSession) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeMissingCameraToolCallingTextProvider>();
  auto vision = std::make_shared<FakeVisionProvider>();
  engine.SetTextProvider(provider);
  engine.SetVisionProvider(vision);

  ravbot::mobile::CameraFrame frame;
  frame.width = 320;
  frame.height = 240;
  frame.format = "YUV420_LUMA";
  frame.timestamp_ms = 4242;
  frame.data.assign(320 * 240, static_cast<uint8_t>(128));
  ASSERT_TRUE(engine.PushCameraFrame("agent:main:camera-a", frame));

  ASSERT_TRUE(
      engine.SendTextTurn("agent:main:camera-b", "What do you see now?"));

  ASSERT_EQ(provider->requests.size(), 2u);
  auto history = engine.session_manager().GetHistory("agent:main:camera-b");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "camera_snapshot");
  EXPECT_NE(history[2].content[0].content.find("\"available\": false"),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"reason\": \"no_frame_yet\""),
            std::string::npos);
  EXPECT_EQ(history[3].content[0].text,
            "No camera snapshot is available for this session.");
}

TEST_F(MobileEngineTest, CameraSnapshotReportsBackgroundGatedReason) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider =
      std::make_shared<FakeUnavailableCameraReasonToolCallingTextProvider>(
          "background_gated");
  engine.SetTextProvider(provider);

  ravbot::mobile::DeviceStatusSnapshot status;
  status.service_running = true;
  status.capture_requested = true;
  status.permissions_granted = true;
  status.camera_status = "running";
  ASSERT_TRUE(engine.ReportDeviceStatus("agent:main:camera-bg", status));

  engine.SetForegroundState(false);
  ASSERT_TRUE(
      engine.SendTextTurn("agent:main:camera-bg", "What do you see now?"));

  ASSERT_EQ(provider->requests.size(), 2u);
  auto history = engine.session_manager().GetHistory("agent:main:camera-bg");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "camera_snapshot");
  EXPECT_NE(history[2].content[0].content.find("\"available\": false"),
            std::string::npos);
  EXPECT_NE(
      history[2].content[0].content.find("\"reason\": \"background_gated\""),
      std::string::npos);
  EXPECT_NE(history[2].content[0].content.find(
                "\"visionProvider\": \"placeholder_mobile_vision\""),
            std::string::npos);
  EXPECT_NE(
      history[2].content[0].content.find("\"visionProviderPlaceholder\": true"),
      std::string::npos);
  const auto camera_tool_result =
      nlohmann::json::parse(history[2].content[0].content);
  EXPECT_EQ(camera_tool_result["hostCapabilities"]["capture"]["reason"],
            "background_gated");
  EXPECT_EQ(camera_tool_result["hostCapabilities"]["camera"]["reason"],
            "background_gated");
  EXPECT_EQ(history[3].content[0].text,
            "Camera snapshot unavailable because background_gated.");
}

TEST_F(MobileEngineTest, StartSessionClearsVolatileCameraStateForSameSession) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeMissingCameraToolCallingTextProvider>();
  auto vision = std::make_shared<FakeVisionProvider>();
  engine.SetTextProvider(provider);
  engine.SetVisionProvider(vision);

  ravbot::mobile::DeviceStatusSnapshot status;
  status.service_running = true;
  status.capture_requested = true;
  status.permissions_granted = true;
  status.camera_status = "running";
  ASSERT_TRUE(engine.ReportDeviceStatus("agent:main:camera-reset", status));

  ravbot::mobile::CameraFrame frame;
  frame.width = 320;
  frame.height = 240;
  frame.format = "YUV420_LUMA";
  frame.timestamp_ms = 4242;
  frame.data.assign(320 * 240, static_cast<uint8_t>(128));
  ASSERT_TRUE(engine.PushCameraFrame("agent:main:camera-reset", frame));

  ASSERT_EQ(engine.StartSession("agent:main:camera-reset"),
            "agent:main:camera-reset");
  ASSERT_TRUE(
      engine.SendTextTurn("agent:main:camera-reset", "What do you see now?"));

  ASSERT_EQ(provider->requests.size(), 2u);
  auto history = engine.session_manager().GetHistory("agent:main:camera-reset");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "camera_snapshot");
  EXPECT_NE(history[2].content[0].content.find("\"available\": false"),
            std::string::npos);
  EXPECT_NE(history[2].content[0].content.find("\"reason\": \"no_frame_yet\""),
            std::string::npos);
  EXPECT_NE(
      history[2].content[0].content.find("\"deviceStatusAvailable\": false"),
      std::string::npos);
  EXPECT_EQ(history[3].content[0].text,
            "No camera snapshot is available for this session.");
}

TEST_F(MobileEngineTest, SendTextTurnExecutesMemoryWriteToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeMemoryWriteToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:memory-write",
                                  "Write this into mobile memory."));

  const auto memory_file = test_dir_ / "workspace" / "notes" / "memory.md";
  ASSERT_TRUE(std::filesystem::exists(memory_file));
  std::ifstream input(memory_file);
  ASSERT_TRUE(input.good());
  const std::string content(std::istreambuf_iterator<char>(input), {});
  EXPECT_EQ(content, "remember the dragonfruit");

  auto tool_result =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolResult &&
                            event.payload.value("name", "") == "memory_write";
                   });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"path\": \"notes/memory.md\""),
            std::string::npos);
}

TEST_F(MobileEngineTest, SendTextTurnExecutesMemorySearchToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeMemorySearchToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  const auto workspace = test_dir_ / "workspace";
  std::filesystem::create_directories(workspace);
  const auto memory_file = workspace / "MEMORY.md";
  {
    std::ofstream output(memory_file);
    output << "dragonfruit project status: green\n";
  }

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:memory-search",
                                  "Search mobile memory for dragonfruit."));

  auto tool_result =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolResult &&
                            event.payload.value("name", "") == "memory_search";
                   });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "dragonfruit project status"),
            std::string::npos);

  auto history =
      engine.session_manager().GetHistory("agent:main:memory-search");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "memory_search");
  EXPECT_EQ(history[3].content[0].text,
            "I found the dragonfruit project note in memory.");
}

TEST_F(MobileEngineTest, SendTextTurnExecutesMemoryListToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeMemoryListToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  const auto notes_dir = test_dir_ / "workspace" / "notes";
  std::filesystem::create_directories(notes_dir / "nested");
  {
    std::ofstream output(notes_dir / "shopping.txt");
    output << "milk\n";
  }
  {
    std::ofstream output(notes_dir / "nested" / "project.md");
    output << "project note\n";
  }

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:memory-list",
                                  "List the mobile memory files."));

  auto tool_result =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolResult &&
                            event.payload.value("name", "") == "memory_list";
                   });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  const auto result = tool_result->payload["result"].get<std::string>();
  EXPECT_NE(result.find("\"path\": \"notes/shopping.txt\""), std::string::npos);
  EXPECT_NE(result.find("\"path\": \"notes/nested/project.md\""),
            std::string::npos);

  auto history = engine.session_manager().GetHistory("agent:main:memory-list");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "memory_list");
  EXPECT_EQ(history[3].content[0].text, "I found the mobile memory files.");
}

TEST_F(MobileEngineTest, SendTextTurnExecutesMemoryDeleteToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeMemoryDeleteToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  const auto memory_file = test_dir_ / "workspace" / "notes" / "trash.md";
  std::filesystem::create_directories(memory_file.parent_path());
  {
    std::ofstream output(memory_file);
    output << "obsolete note\n";
  }
  ASSERT_TRUE(std::filesystem::exists(memory_file));

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:memory-delete",
                                  "Delete the obsolete note."));

  EXPECT_FALSE(std::filesystem::exists(memory_file));
  auto tool_result =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolResult &&
                            event.payload.value("name", "") == "memory_delete";
                   });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"removedCount\": 1"),
            std::string::npos);

  auto history =
      engine.session_manager().GetHistory("agent:main:memory-delete");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "memory_delete");
  EXPECT_EQ(history[3].content[0].text, "Mobile memory delete completed.");
}

TEST_F(MobileEngineTest, SendTextTurnExecutesTimeToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeTimeToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(
      engine.SendTextTurn("agent:main:time", "What time is it on device?"));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "time"), names.end());

  auto tool_result =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolResult &&
                            event.payload.value("name", "") == "time";
                   });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  const auto result = tool_result->payload["result"].get<std::string>();
  EXPECT_NE(result.find("\"epochMs\""), std::string::npos);
  EXPECT_NE(result.find("\"utc\""), std::string::npos);
  EXPECT_NE(result.find("\"local\""), std::string::npos);
  EXPECT_NE(result.find("\"utcOffset\""), std::string::npos);

  auto history = engine.session_manager().GetHistory("agent:main:time");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "time");
  EXPECT_EQ(history[3].content[0].text, "Device time snapshot received.");
}

TEST_F(MobileEngineTest, SendTextTurnExecutesRuntimeStatusToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  engine.SetDeviceBridge(std::make_shared<FakeDeviceBridge>());
  auto provider = std::make_shared<FakeRuntimeStatusToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:runtime-tool",
                                  "What runtime capabilities are ready?"));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "runtime_status"),
            names.end());

  auto tool_result =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolResult &&
                            event.payload.value("name", "") == "runtime_status";
                   });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  const auto result = tool_result->payload["result"].get<std::string>();
  EXPECT_NE(result.find("\"deviceBridgeAttached\": true"), std::string::npos);
  EXPECT_NE(result.find("\"webSearchReady\": true"), std::string::npos);
  EXPECT_NE(result.find("\"webFetchReady\": true"), std::string::npos);
  EXPECT_NE(result.find("\"vibrationReady\": true"), std::string::npos);
  EXPECT_NE(result.find("\"availableTools\""), std::string::npos);
  EXPECT_NE(result.find("\"web_search\""), std::string::npos);
  EXPECT_NE(result.find("\"toolAvailability\""), std::string::npos);
  EXPECT_NE(result.find("\"provider\": \"fake-runtime-tool\""),
            std::string::npos);

  auto history = engine.session_manager().GetHistory("agent:main:runtime-tool");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "runtime_status");
  EXPECT_EQ(history[3].content[0].text, "Runtime readiness snapshot received.");
}

TEST_F(MobileEngineTest, SendTextTurnExecutesVibrateToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto bridge = std::make_shared<FakeDeviceBridge>();
  engine.SetDeviceBridge(bridge);
  auto provider = std::make_shared<FakeVibrateToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(
      engine.SendTextTurn("agent:main:vibrate", "Trigger device haptics."));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "vibrate"), names.end());
  ASSERT_EQ(bridge->vibration_calls.size(), 1u);
  EXPECT_EQ(bridge->vibration_calls.front(), "agent:main:vibrate:180");

  auto tool_result =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolResult &&
                            event.payload.value("name", "") == "vibrate";
                   });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"durationMs\": 180"),
            std::string::npos);

  auto history = engine.session_manager().GetHistory("agent:main:vibrate");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "vibrate");
  EXPECT_EQ(history[3].content[0].text, "Device haptic pulse triggered.");
}

TEST_F(MobileEngineTest, ToolSchemasExposeWebToolsOnlyWithDeviceBridge) {
  ravbot::mobile::MobileEngine without_bridge(MakeConfig(), test_dir_,
                                              test_dir_, logger_);
  auto plain_provider =
      std::make_shared<FakeTextProvider>("reply without bridge");
  without_bridge.SetTextProvider(plain_provider);

  ASSERT_TRUE(without_bridge.SendTextTurn("agent:main:no-web-tools", "hello"));
  auto without_names = tool_names(plain_provider->last_request_);
  EXPECT_EQ(std::find(without_names.begin(), without_names.end(), "web_search"),
            without_names.end());
  EXPECT_EQ(std::find(without_names.begin(), without_names.end(), "web_fetch"),
            without_names.end());

  ravbot::mobile::MobileEngine with_speech_only_bridge(MakeConfig(), test_dir_,
                                                       test_dir_, logger_);
  auto speech_only_bridge = std::make_shared<FakeSpeechOnlyDeviceBridge>();
  auto speech_only_provider =
      std::make_shared<FakeTextProvider>("reply with speech-only bridge");
  with_speech_only_bridge.SetDeviceBridge(speech_only_bridge);
  with_speech_only_bridge.SetTextProvider(speech_only_provider);

  ASSERT_TRUE(with_speech_only_bridge.SendTextTurn(
      "agent:main:speech-only-web-tools", "hello"));
  auto speech_only_names = tool_names(speech_only_provider->last_request_);
  EXPECT_EQ(std::find(speech_only_names.begin(), speech_only_names.end(),
                      "web_search"),
            speech_only_names.end());
  EXPECT_EQ(std::find(speech_only_names.begin(), speech_only_names.end(),
                      "web_fetch"),
            speech_only_names.end());

  ravbot::mobile::MobileEngine with_bridge(MakeConfig(), test_dir_, test_dir_,
                                           logger_);
  auto bridge = std::make_shared<FakeDeviceBridge>();
  auto bridge_provider =
      std::make_shared<FakeTextProvider>("reply with bridge");
  with_bridge.SetDeviceBridge(bridge);
  with_bridge.SetTextProvider(bridge_provider);

  ASSERT_TRUE(with_bridge.SendTextTurn("agent:main:with-web-tools", "hello"));
  auto with_names = tool_names(bridge_provider->last_request_);
  EXPECT_NE(std::find(with_names.begin(), with_names.end(), "web_search"),
            with_names.end());
  EXPECT_NE(std::find(with_names.begin(), with_names.end(), "web_fetch"),
            with_names.end());
}

TEST_F(MobileEngineTest, SendTextTurnExecutesWebSearchToolThroughDeviceBridge) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeWebSearchToolCallingTextProvider>();
  auto bridge = std::make_shared<FakeDeviceBridge>();
  engine.SetTextProvider(provider);
  engine.SetDeviceBridge(bridge);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:web-search",
                                  "Search the web for RavBot Android."));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "web_search"), names.end());
  ASSERT_EQ(bridge->web_search_calls.size(), 1u);
  EXPECT_EQ(std::get<0>(bridge->web_search_calls.front()),
            "agent:main:web-search");
  EXPECT_EQ(std::get<1>(bridge->web_search_calls.front()),
            "ravbot android mvp");
  EXPECT_EQ(std::get<2>(bridge->web_search_calls.front()), 3);

  auto tool_result =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolResult &&
                            event.payload.value("name", "") == "web_search";
                   });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "RavBot Android MVP"),
            std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"sessionKey\": \"agent:main:web-search\""),
            std::string::npos);

  auto history = engine.session_manager().GetHistory("agent:main:web-search");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "web_search");
  EXPECT_EQ(history[3].content[0].text,
            "I found the RavBot Android MVP search result.");
}

TEST_F(MobileEngineTest, SendTextTurnExecutesWebFetchToolThroughDeviceBridge) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeWebFetchToolCallingTextProvider>();
  auto bridge = std::make_shared<FakeDeviceBridge>();
  engine.SetTextProvider(provider);
  engine.SetDeviceBridge(bridge);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents([&events](const ravbot::mobile::MobileEvent& event) {
    events.push_back(event);
  });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:web-fetch",
                                  "Fetch the RavBot Android page."));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "web_fetch"), names.end());
  ASSERT_EQ(bridge->web_fetch_calls.size(), 1u);
  EXPECT_EQ(std::get<0>(bridge->web_fetch_calls.front()),
            "agent:main:web-fetch");
  EXPECT_EQ(std::get<1>(bridge->web_fetch_calls.front()),
            "https://example.com/ravbot-android");
  EXPECT_EQ(std::get<2>(bridge->web_fetch_calls.front()), 4096);

  auto tool_result =
      std::find_if(events.begin(), events.end(),
                   [](const ravbot::mobile::MobileEvent& event) {
                     return event.name == ravbot::mobile::kEventToolResult &&
                            event.payload.value("name", "") == "web_fetch";
                   });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "Android embodied assistant page content."),
            std::string::npos);
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"sessionKey\": \"agent:main:web-fetch\""),
            std::string::npos);

  auto history = engine.session_manager().GetHistory("agent:main:web-fetch");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "web_fetch");
  EXPECT_EQ(history[3].content[0].text,
            "I fetched the Android embodied assistant page.");
}

}  // namespace
