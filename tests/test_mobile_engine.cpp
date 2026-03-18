// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>

#include "ravbot/mobile/mobile_engine.hpp"

namespace {

class FakeTextProvider : public ravbot::LLMProvider {
 public:
  explicit FakeTextProvider(std::string reply) : reply_(std::move(reply)) {}

  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
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

  std::string GetProviderName() const override { return "fake"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-model"};
  }

  ravbot::ChatCompletionRequest last_request_;

 private:
  std::string reply_;
};

class FakeStreamingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
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

  std::string GetProviderName() const override { return "fake-stream"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-stream-model"};
  }

  ravbot::ChatCompletionRequest last_request_;
};

class FakeToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
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

  std::string GetProviderName() const override { return "fake-tool"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-tool-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeCameraToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
    ravbot::ChatCompletionResponse response;
    response.finish_reason = "stop";
    if (!request.messages.empty()) {
      const auto& last = request.messages.back();
      if (last.role == "user" && !last.content.empty() &&
          last.content.front().type == "tool_result" &&
          last.content.front().content.find("desk with phone") !=
              std::string::npos) {
        response.content =
            "Latest camera observation shows a desk with phone.";
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

  std::string GetProviderName() const override { return "fake-camera-tool"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-camera-tool-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeMemoryWriteToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
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

  std::string GetProviderName() const override { return "fake-memory-write"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-memory-write-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeMemorySearchToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
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

  std::string GetProviderName() const override { return "fake-memory-search"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-memory-search-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeMemoryListToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
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

  std::string GetProviderName() const override { return "fake-memory-list"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-memory-list-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeMemoryDeleteToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
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

  std::string GetProviderName() const override { return "fake-memory-delete"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-memory-delete-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeTimeToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
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

  std::string GetProviderName() const override { return "fake-time-tool"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-time-tool-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeWebSearchToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
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

  std::string GetProviderName() const override { return "fake-web-search"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-web-search-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

class FakeWebFetchToolCallingTextProvider : public ravbot::LLMProvider {
 public:
  ravbot::ChatCompletionResponse ChatCompletion(
      const ravbot::ChatCompletionRequest& request) override {
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

  std::string GetProviderName() const override { return "fake-web-fetch"; }
  std::vector<std::string> GetSupportedModels() const override {
    return {"fake-web-fetch-model"};
  }

  std::vector<ravbot::ChatCompletionRequest> requests;
};

std::vector<std::string> tool_names(
    const ravbot::ChatCompletionRequest& request) {
  std::vector<std::string> names;
  for (const auto& tool : request.tools) {
    names.push_back(tool["function"].value("name", ""));
  }
  return names;
}

class FakeAsrProvider : public ravbot::mobile::MobileAsrProvider {
 public:
  ravbot::mobile::AsrUpdate PushPcm16(const int16_t* samples,
                                      size_t sample_count,
                                      int sample_rate_hz,
                                      bool end_of_turn) override {
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    has_pending_audio = true;
    if (end_of_turn) {
      has_pending_audio = false;
      ravbot::mobile::AsrUpdate update;
      update.text = "ni hao ravbot";
      update.is_final = true;
      update.segment_index = 1;
      update.sample_rate_hz = sample_rate_hz;
      update.duration_ms = 120;
      update.average_level = 0.12;
      update.peak_level = 0.34;
      update.end_reason = "vad_silence";
      return update;
    }
    ravbot::mobile::AsrUpdate update;
    update.text = "ni hao";
    update.is_final = false;
    update.segment_index = 1;
    update.sample_rate_hz = sample_rate_hz;
    update.duration_ms = 60;
    update.average_level = 0.1;
    update.peak_level = 0.2;
    update.end_reason = "streaming";
    return update;
  }

  ravbot::mobile::AsrUpdate Flush() override {
    if (!has_pending_audio) {
      return {};
    }
    has_pending_audio = false;
    ravbot::mobile::AsrUpdate update;
    update.text = "ni hao ravbot";
    update.is_final = true;
    update.segment_index = 1;
    update.sample_rate_hz = 16000;
    update.duration_ms = 180;
    update.average_level = 0.14;
    update.peak_level = 0.36;
    update.end_reason = "flush";
    return update;
  }

  void Interrupt() override {
    interrupted = true;
    has_pending_audio = false;
  }

  bool interrupted = false;
  bool has_pending_audio = false;
};

class FakeDeviceBridge : public ravbot::mobile::DeviceCapabilityBridge {
 public:
  std::string ResolveStateDirectory() const override { return "/tmp/state"; }
  std::string ResolveModelsDirectory() const override { return "/tmp/models"; }
  bool IsForeground() const override { return true; }

  void SetAvatarState(ravbot::mobile::AvatarState state) override {
    avatar_states.push_back(ravbot::mobile::AvatarStateToString(state));
  }

  void RequestSpeechPlayback(const std::string& text) override {
    speech_requests.push_back(text);
  }

  void InterruptSpeechPlayback() override { speech_interrupts += 1; }

  std::string WebSearch(const std::string& query,
                        int count,
                        const std::string& freshness) override {
    web_search_calls.push_back({query, count, freshness});
    return nlohmann::json{
        {"provider", "android_host"},
        {"query", query},
        {"freshness", freshness},
        {"results",
         nlohmann::json::array(
             {{{"title", "RavBot Android MVP"},
               {"url", "https://example.com/ravbot-android"},
               {"description", "Android embodied assistant planning page."}}})}}
        .dump(2);
  }

  std::string WebFetch(const std::string& url, int max_chars) override {
    web_fetch_calls.push_back({url, max_chars});
    return nlohmann::json{
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
  std::vector<std::tuple<std::string, int, std::string>> web_search_calls;
  std::vector<std::pair<std::string, int>> web_fetch_calls;
};

class FakeVisionProvider : public ravbot::mobile::MobileVisionProvider {
 public:
  std::optional<std::string> ObserveFrame(
      const ravbot::mobile::CameraFrame& frame) override {
    ++call_count;
    last_width = frame.width;
    return "desk with phone";
  }

  int call_count = 0;
  int last_width = 0;
};

class MobileEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("mobile-engine-test", sink);
    test_dir_ = std::filesystem::temp_directory_path() /
                "ravbot_mobile_engine_test";
    std::filesystem::remove_all(test_dir_);
    std::filesystem::create_directories(test_dir_);
  }

  void TearDown() override { std::filesystem::remove_all(test_dir_); }

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
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
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
  status.microphone_status = "running";
  status.camera_status = "running";
  status.speaker_status = "speaking";
  status.timestamp_ms = 42;

  ASSERT_TRUE(engine.ReportDeviceStatus(status));
  auto first_status = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileDeviceStatus;
      });
  ASSERT_NE(first_status, events.end());
  EXPECT_TRUE(first_status->payload["foreground"]);
  EXPECT_TRUE(first_status->payload["serviceRunning"]);
  EXPECT_EQ(first_status->payload["microphoneStatus"], "running");
  EXPECT_EQ(first_status->payload["speakerStatus"], "speaking");

  engine.SetForegroundState(false);
  auto latest_status = std::find_if(
      events.rbegin(), events.rend(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileDeviceStatus;
      });
  ASSERT_NE(latest_status, events.rend());
  EXPECT_FALSE(latest_status->payload["foreground"]);
  EXPECT_TRUE(latest_status->payload["serviceRunning"]);
}

TEST_F(MobileEngineTest, PushPcm16UsesAsrProviderForFinalTurn) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  engine.SetTextProvider(
      std::make_shared<FakeTextProvider>("speech reply"));
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
  auto asr_final = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileAsrFinal;
      });
  ASSERT_NE(asr_final, events.end());
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
  ASSERT_TRUE(engine.PushPcm16("agent:main:speech-flush", samples, 4, 16000,
                               false));
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
  EXPECT_EQ(asr_partial->payload["endReason"], "streaming");

  auto asr_final = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileAsrFinal;
      });
  ASSERT_NE(asr_final, events.end());
  EXPECT_EQ(asr_final->payload["segmentIndex"], 1);
  EXPECT_EQ(asr_final->payload["durationMs"], 180);
  EXPECT_EQ(asr_final->payload["endReason"], "flush");
}

TEST_F(MobileEngineTest, PushCameraFrameRequiresForegroundWhenConfigured) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto vision = std::make_shared<FakeVisionProvider>();
  engine.SetVisionProvider(vision);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
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
  auto it = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileVisionObservation;
      });
  EXPECT_NE(it, events.end());
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

TEST_F(MobileEngineTest, StartSessionEmitsRuntimeStatusWithResolvedModelPaths) {
  auto config = MakeConfig();
  config.mobile.models.llm_model = "Qwen3.5-0.8B-Q4_K_M.gguf";
  config.mobile.models.vlm_model = "SmolVLM-500M-Instruct-Q8_0.gguf";
  config.mobile.models.mmproj_model =
      "mmproj-SmolVLM-500M-Instruct-Q8_0.gguf";
  ravbot::mobile::MobileEngine engine(config, test_dir_, test_dir_, logger_);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
        events.push_back(event);
      });

  ASSERT_EQ(engine.StartSession("agent:main:runtime-status"),
            "agent:main:runtime-status");

  auto it = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventMobileRuntimeStatus;
      });
  ASSERT_NE(it, events.end());
  EXPECT_EQ(it->payload["provider"], "llama_cpp_mobile");
  EXPECT_EQ(it->payload["speechProvider"], "sherpa_onnx_mobile");
  EXPECT_EQ(it->payload["modelsDir"], test_dir_.string());
  EXPECT_EQ(it->payload["llmModelPath"],
            (test_dir_ / "Qwen3.5-0.8B-Q4_K_M.gguf").string());
  EXPECT_EQ(it->payload["sttModelPath"], (test_dir_ / "SenseVoiceSmall").string());
  EXPECT_FALSE(it->payload["llmModelExists"].get<bool>());
  EXPECT_FALSE(it->payload["sttModelExists"].get<bool>());
}

TEST_F(MobileEngineTest, ReportTtsPlaybackStateEmitsEventAndAvatarState) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
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
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
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
  status.microphone_status = "running";
  status.camera_status = "running";
  status.speaker_status = "speaking";
  ASSERT_TRUE(engine.ReportDeviceStatus(status));

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
        events.push_back(event);
      });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:tooling", "How is the device?"));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "device_status"),
            names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "camera_snapshot"),
            names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "memory_list"),
            names.end());
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
  EXPECT_EQ(history[3].role, "assistant");
  EXPECT_EQ(history[3].content[0].text,
            "Device status received. Service is running and speaker is "
            "speaking.");

  auto tool_start = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventToolStart;
      });
  ASSERT_NE(tool_start, events.end());
  EXPECT_EQ(tool_start->payload["name"], "device_status");

  auto tool_result = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventToolResult;
      });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"speakerStatus\": \"speaking\""),
            std::string::npos);

  auto assistant_final = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventAssistantFinal;
      });
  ASSERT_NE(assistant_final, events.end());
  EXPECT_EQ(assistant_final->payload["finishReason"], "stop");
}

TEST_F(MobileEngineTest, SendTextTurnExecutesCameraSnapshotToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeCameraToolCallingTextProvider>();
  auto vision = std::make_shared<FakeVisionProvider>();
  engine.SetTextProvider(provider);
  engine.SetVisionProvider(vision);

  ravbot::mobile::CameraFrame frame;
  frame.width = 320;
  frame.height = 240;
  frame.format = "YUV420_LUMA";
  frame.timestamp_ms = 4242;
  frame.data.assign(320 * 240, static_cast<uint8_t>(128));
  ASSERT_TRUE(engine.PushCameraFrame("agent:main:camera-tool", frame));

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
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
  EXPECT_NE(history[2].content[0].content.find("\"summary\": \"desk with phone\""),
            std::string::npos);
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
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "\"timestampMs\": 4242"),
            std::string::npos);
}

TEST_F(MobileEngineTest, SendTextTurnExecutesMemoryWriteToolRoundTrip) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeMemoryWriteToolCallingTextProvider>();
  engine.SetTextProvider(provider);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
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

  auto tool_result = std::find_if(
      events.begin(), events.end(),
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
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
        events.push_back(event);
      });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:memory-search",
                                  "Search mobile memory for dragonfruit."));

  auto tool_result = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventToolResult &&
               event.payload.value("name", "") == "memory_search";
      });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "dragonfruit project status"),
            std::string::npos);

  auto history = engine.session_manager().GetHistory("agent:main:memory-search");
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
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
        events.push_back(event);
      });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:memory-list",
                                  "List the mobile memory files."));

  auto tool_result = std::find_if(
      events.begin(), events.end(),
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
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
        events.push_back(event);
      });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:memory-delete",
                                  "Delete the obsolete note."));

  EXPECT_FALSE(std::filesystem::exists(memory_file));
  auto tool_result = std::find_if(
      events.begin(), events.end(),
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
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
        events.push_back(event);
      });

  ASSERT_TRUE(
      engine.SendTextTurn("agent:main:time", "What time is it on device?"));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "time"), names.end());

  auto tool_result = std::find_if(
      events.begin(), events.end(),
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

TEST_F(MobileEngineTest, SendTextTurnExecutesWebSearchToolThroughDeviceBridge) {
  ravbot::mobile::MobileEngine engine(MakeConfig(), test_dir_, test_dir_,
                                      logger_);
  auto provider = std::make_shared<FakeWebSearchToolCallingTextProvider>();
  auto bridge = std::make_shared<FakeDeviceBridge>();
  engine.SetTextProvider(provider);
  engine.SetDeviceBridge(bridge);

  std::vector<ravbot::mobile::MobileEvent> events;
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
        events.push_back(event);
      });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:web-search",
                                  "Search the web for RavBot Android."));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "web_search"), names.end());
  ASSERT_EQ(bridge->web_search_calls.size(), 1u);
  EXPECT_EQ(std::get<0>(bridge->web_search_calls.front()), "ravbot android mvp");
  EXPECT_EQ(std::get<1>(bridge->web_search_calls.front()), 3);

  auto tool_result = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventToolResult &&
               event.payload.value("name", "") == "web_search";
      });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "RavBot Android MVP"),
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
  engine.SubscribeEvents(
      [&events](const ravbot::mobile::MobileEvent& event) {
        events.push_back(event);
      });

  ASSERT_TRUE(engine.SendTextTurn("agent:main:web-fetch",
                                  "Fetch the RavBot Android page."));

  ASSERT_EQ(provider->requests.size(), 2u);
  const auto names = tool_names(provider->requests.front());
  EXPECT_NE(std::find(names.begin(), names.end(), "web_fetch"), names.end());
  ASSERT_EQ(bridge->web_fetch_calls.size(), 1u);
  EXPECT_EQ(bridge->web_fetch_calls.front().first,
            "https://example.com/ravbot-android");
  EXPECT_EQ(bridge->web_fetch_calls.front().second, 4096);

  auto tool_result = std::find_if(
      events.begin(), events.end(),
      [](const ravbot::mobile::MobileEvent& event) {
        return event.name == ravbot::mobile::kEventToolResult &&
               event.payload.value("name", "") == "web_fetch";
      });
  ASSERT_NE(tool_result, events.end());
  EXPECT_EQ(tool_result->payload["status"], "ok");
  EXPECT_NE(tool_result->payload["result"].get<std::string>().find(
                "Android embodied assistant page content."),
            std::string::npos);

  auto history = engine.session_manager().GetHistory("agent:main:web-fetch");
  ASSERT_EQ(history.size(), 4u);
  EXPECT_EQ(history[1].content[0].name, "web_fetch");
  EXPECT_EQ(history[3].content[0].text,
            "I fetched the Android embodied assistant page.");
}

}  // namespace
