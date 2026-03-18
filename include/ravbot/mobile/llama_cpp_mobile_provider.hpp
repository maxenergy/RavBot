// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include "ravbot/config.hpp"
#include "ravbot/mobile/mobile_backend.hpp"

namespace ravbot::mobile {

class LlamaCppMobileProvider : public MobileBackend {
 public:
  explicit LlamaCppMobileProvider(const MobileModelsConfig& config,
                                  std::shared_ptr<spdlog::logger> logger);

  std::string Name() const override { return "llama.cpp-mobile"; }
  bool IsReady() const override;
  nlohmann::json SendTextTurn(const std::string& session_key,
                              const std::string& text) override;
  nlohmann::json PushPcm16(const std::string& session_key,
                           const int16_t* samples,
                           size_t sample_count,
                           int sample_rate) override;
  nlohmann::json PushCameraFrame(const std::string& session_key,
                                 int width,
                                 int height,
                                 int64_t timestamp_ms) override;
  void Interrupt() override;

 private:
  MobileModelsConfig config_;
  std::shared_ptr<spdlog::logger> logger_;
  bool interrupted_ = false;
};

}  // namespace ravbot::mobile
