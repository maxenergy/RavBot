// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace ravbot::mobile {

class MobileBackend {
 public:
  virtual ~MobileBackend() = default;

  virtual std::string Name() const = 0;
  virtual bool IsReady() const = 0;
  virtual nlohmann::json SendTextTurn(const std::string& session_key,
                                      const std::string& text) = 0;
  virtual nlohmann::json PushPcm16(const std::string& session_key,
                                   const int16_t* samples,
                                   size_t sample_count,
                                   int sample_rate) = 0;
  virtual nlohmann::json PushCameraFrame(const std::string& session_key,
                                         int width,
                                         int height,
                                         int64_t timestamp_ms) = 0;
  virtual void Interrupt() = 0;
};

}  // namespace ravbot::mobile
