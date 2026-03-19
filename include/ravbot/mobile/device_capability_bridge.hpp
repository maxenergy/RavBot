// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "ravbot/mobile/mobile_events.hpp"

namespace ravbot::mobile {

class DeviceCapabilityBridge {
 public:
  virtual ~DeviceCapabilityBridge() = default;

  virtual std::string ResolveStateDirectory() const = 0;
  virtual std::string ResolveModelsDirectory() const = 0;
  virtual bool IsForeground() const = 0;
  virtual bool SupportsWebSearch() const = 0;
  virtual bool SupportsWebFetch() const = 0;
  virtual bool SupportsVibration() const { return false; }

  virtual void SetAvatarState(AvatarState state) = 0;
  virtual void RequestSpeechPlayback(const std::string& text) = 0;
  virtual void InterruptSpeechPlayback() = 0;
  virtual void Vibrate(int duration_ms) { (void)duration_ms; }
  virtual std::string WebSearch(const std::string& query,
                                int count,
                                const std::string& freshness) = 0;
  virtual std::string WebFetch(const std::string& url, int max_chars) = 0;
};

}  // namespace ravbot::mobile
