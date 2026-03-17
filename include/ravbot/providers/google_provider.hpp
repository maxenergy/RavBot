// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include "ravbot/providers/curl_raii.hpp"

#include "llm_provider.hpp"

namespace ravbot {

class GoogleProvider : public LLMProvider {
 public:
  GoogleProvider(const std::string& api_key, const std::string& base_url,
                 int timeout, std::shared_ptr<spdlog::logger> logger);

  ChatCompletionResponse
  ChatCompletion(const ChatCompletionRequest& request) override;
  void ChatCompletionStream(
      const ChatCompletionRequest& request,
      std::function<void(const ChatCompletionResponse&)> callback) override;
  std::string GetProviderName() const override;
  std::vector<std::string> GetSupportedModels() const override;

 protected:
  virtual std::string MakeApiRequest(const std::string& model,
                                     const std::string& json_payload,
                                     bool stream) const;

 private:
  nlohmann::json
  ConvertRequestToGemini(const ChatCompletionRequest& request) const;
  ChatCompletionResponse
  ConvertGeminiResponse(const nlohmann::json& response) const;
  CurlSlist CreateHeaders() const;

  std::string api_key_;
  std::string base_url_;
  int timeout_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace ravbot
