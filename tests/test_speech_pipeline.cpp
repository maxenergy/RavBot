// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <memory>

#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>

#include "ravbot/mobile/speech_pipeline.hpp"

namespace {

std::shared_ptr<spdlog::logger> MakeLogger() {
  auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>("speech-pipeline-test", sink);
}

class SpeechPipelineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = std::filesystem::temp_directory_path() /
                "ravbot_speech_pipeline_test";
    std::filesystem::remove_all(test_dir_);
    std::filesystem::create_directories(test_dir_);
  }

  void TearDown() override { std::filesystem::remove_all(test_dir_); }

  std::filesystem::path test_dir_;
};

TEST_F(SpeechPipelineTest, ResolvesRelativeAssetPathsAgainstModelsDir) {
  std::filesystem::path stt_dir = test_dir_ / "SenseVoiceSmall";
  std::filesystem::path tts_dir = test_dir_ / "kokoro-multi-lang-v1_1";
  std::filesystem::create_directories(stt_dir);
  std::filesystem::create_directories(tts_dir);

  ravbot::MobileModelsConfig config;
  config.stt_model = "SenseVoiceSmall";
  config.tts_voice = "kokoro-multi-lang-v1_1";

  ravbot::mobile::SpeechPipeline pipeline(config, test_dir_, MakeLogger());
  const auto& status = pipeline.runtime_status();

  EXPECT_EQ(status.stt_model_path, stt_dir.string());
  EXPECT_EQ(status.tts_voice_path, tts_dir.string());
  EXPECT_TRUE(status.stt_model_exists);
  EXPECT_TRUE(status.tts_voice_exists);
  EXPECT_EQ(status.asr_ready, status.backend_linked);
  EXPECT_EQ(status.tts_ready, status.backend_linked);
}

TEST_F(SpeechPipelineTest, EmitsPlaceholderTranscriptsUntilRealBackendExists) {
  ravbot::MobileModelsConfig config;
  config.stt_model = "SenseVoiceSmall";
  config.tts_voice = "kokoro-multi-lang-v1_1";

  ravbot::mobile::SpeechPipeline pipeline(config, test_dir_, MakeLogger());
  int16_t samples[4] = {1, 2, 3, 4};

  auto partial = pipeline.PushPcm16("session-alpha", samples, 4, 16000, false);
  EXPECT_NE(partial.text.find("listening on device"), std::string::npos);
  EXPECT_FALSE(partial.is_final);
  EXPECT_EQ(partial.segment_index, 1u);
  EXPECT_EQ(partial.sample_rate_hz, 16000);
  EXPECT_EQ(partial.end_reason, "streaming");
  EXPECT_GT(partial.duration_ms, 0);

  auto final = pipeline.Flush("session-alpha");
  EXPECT_NE(final.text.find("audio segment 1 received on device"),
            std::string::npos);
  EXPECT_TRUE(final.is_final);
  EXPECT_EQ(final.segment_index, 1u);
  EXPECT_EQ(final.sample_rate_hz, 16000);
  EXPECT_EQ(final.end_reason, "flush");
  EXPECT_GT(final.duration_ms, 0);
  EXPECT_GE(final.peak_level, final.average_level);
}

TEST_F(SpeechPipelineTest, ThrottlesPartialEventsAcrossContinuousSpeech) {
  ravbot::MobileModelsConfig config;
  config.stt_model = "SenseVoiceSmall";

  ravbot::mobile::SpeechPipeline pipeline(config, test_dir_, MakeLogger());
  int16_t samples[8] = {1, 2, 3, 4, 5, 6, 7, 8};

  auto first = pipeline.PushPcm16("session-alpha", samples, 8, 16000, false);
  auto second = pipeline.PushPcm16("session-alpha", samples, 8, 16000, false);
  auto third = pipeline.PushPcm16("session-alpha", samples, 8, 16000, false);

  EXPECT_FALSE(first.text.empty());
  EXPECT_TRUE(second.text.empty());
  EXPECT_FALSE(third.text.empty());
  EXPECT_FALSE(third.is_final);
  EXPECT_EQ(third.end_reason, "streaming");
}

TEST_F(SpeechPipelineTest, InterruptDropsBufferedSpeechTurn) {
  ravbot::MobileModelsConfig config;
  config.stt_model = "SenseVoiceSmall";

  ravbot::mobile::SpeechPipeline pipeline(config, test_dir_, MakeLogger());
  int16_t samples[4] = {1, 2, 3, 4};

  EXPECT_FALSE(
      pipeline.PushPcm16("session-alpha", samples, 4, 16000, false).text.empty());
  pipeline.Interrupt("session-alpha");
  EXPECT_TRUE(pipeline.Flush("session-alpha").text.empty());

  auto restarted = pipeline.PushPcm16("session-alpha", samples, 4, 16000, true);
  EXPECT_NE(restarted.text.find("audio segment 1 received on device"),
            std::string::npos);
  EXPECT_TRUE(restarted.is_final);
  EXPECT_EQ(restarted.end_reason, "vad_silence");
}

TEST_F(SpeechPipelineTest, KeepsBufferedSpeechScopedPerSession) {
  ravbot::MobileModelsConfig config;
  config.stt_model = "SenseVoiceSmall";

  ravbot::mobile::SpeechPipeline pipeline(config, test_dir_, MakeLogger());
  int16_t samples[4] = {1, 2, 3, 4};

  auto alpha_partial =
      pipeline.PushPcm16("session-alpha", samples, 4, 16000, false);
  auto beta_partial =
      pipeline.PushPcm16("session-beta", samples, 4, 16000, false);
  pipeline.Interrupt("session-beta");
  auto alpha_final = pipeline.Flush("session-alpha");
  auto beta_final = pipeline.Flush("session-beta");

  EXPECT_FALSE(alpha_partial.text.empty());
  EXPECT_EQ(alpha_partial.segment_index, 1u);
  EXPECT_FALSE(beta_partial.text.empty());
  EXPECT_EQ(beta_partial.segment_index, 1u);
  EXPECT_TRUE(alpha_final.is_final);
  EXPECT_EQ(alpha_final.segment_index, 1u);
  EXPECT_EQ(alpha_final.end_reason, "flush");
  EXPECT_TRUE(beta_final.text.empty());

  auto alpha_restart =
      pipeline.PushPcm16("session-alpha", samples, 4, 16000, true);
  auto beta_restart =
      pipeline.PushPcm16("session-beta", samples, 4, 16000, true);
  EXPECT_EQ(alpha_restart.segment_index, 2u);
  EXPECT_EQ(beta_restart.segment_index, 1u);
}

TEST_F(SpeechPipelineTest, ResetSessionClearsBufferedSpeechForThatSessionOnly) {
  ravbot::MobileModelsConfig config;
  config.stt_model = "SenseVoiceSmall";

  ravbot::mobile::SpeechPipeline pipeline(config, test_dir_, MakeLogger());
  int16_t samples[4] = {1, 2, 3, 4};

  EXPECT_FALSE(
      pipeline.PushPcm16("session-alpha", samples, 4, 16000, false).text.empty());
  EXPECT_FALSE(
      pipeline.PushPcm16("session-beta", samples, 4, 16000, false).text.empty());

  pipeline.ResetSession("session-alpha");

  EXPECT_TRUE(pipeline.Flush("session-alpha").text.empty());
  auto beta_final = pipeline.Flush("session-beta");
  EXPECT_TRUE(beta_final.is_final);
  EXPECT_EQ(beta_final.segment_index, 1u);

  auto alpha_restart =
      pipeline.PushPcm16("session-alpha", samples, 4, 16000, true);
  EXPECT_TRUE(alpha_restart.is_final);
  EXPECT_EQ(alpha_restart.segment_index, 1u);
}

}  // namespace
