// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "ravbot/providers/llm_provider.hpp"

namespace ravbot {

class LlamaCppMobileProvider : public LLMProvider {
 public:
  struct Options {
    std::string model_path;
    std::string vision_model_path;
    std::string mmproj_path;
    std::string models_dir;
    bool enable_vulkan = true;
  };

  struct RuntimeStatus {
    bool ready = false;
    bool text_ready = false;
    bool vision_ready = false;
    bool backend_linked = false;
    bool llm_model_exists = false;
    bool vision_model_exists = false;
    bool mmproj_exists = false;
    bool vulkan_requested = true;
    bool vulkan_enabled = false;
    std::string models_dir;
    std::string llm_model_path;
    std::string vision_model_path;
    std::string mmproj_path;
    std::string detail;

    nlohmann::json ToJson() const;
  };

  explicit LlamaCppMobileProvider(
      Options options,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  ChatCompletionResponse ChatCompletion(
      const ChatCompletionRequest& request) override;
  void ChatCompletionStream(
      const ChatCompletionRequest& request,
      std::function<void(const ChatCompletionResponse&)> callback) override;
  std::string GetProviderName() const override { return "llama_cpp_mobile"; }
  std::vector<std::string> GetSupportedModels() const override;

  const Options& options() const { return options_; }
  const RuntimeStatus& runtime_status() const { return runtime_status_; }

 private:
  RuntimeStatus BuildRuntimeStatus() const;

  Options options_;
  std::shared_ptr<spdlog::logger> logger_;
  RuntimeStatus runtime_status_;
};

}  // namespace ravbot
