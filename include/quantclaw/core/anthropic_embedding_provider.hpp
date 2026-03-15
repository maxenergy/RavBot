// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "quantclaw/core/embedding_provider.hpp"

#include <spdlog/spdlog.h>
#include <memory>
#include <string>

namespace quantclaw {

// Anthropic Embedding Provider (Voyage AI)
// Supports voyage-3, voyage-3-lite, voyage-code-3 models
class AnthropicEmbeddingProvider : public EmbeddingProvider {
 public:
  AnthropicEmbeddingProvider(const std::string& api_key,
                            const std::string& model = "voyage-3",
                            std::shared_ptr<spdlog::logger> logger = nullptr);
  ~AnthropicEmbeddingProvider() override = default;

  std::vector<float> Embed(const std::string& text) override;

  std::vector<std::vector<float>> EmbedBatch(
      const std::vector<std::string>& texts) override;

  int GetDimension() const override;

  std::string GetName() const override { return "anthropic"; }

  std::string GetModel() const override { return model_; }

  bool IsAvailable() const override { return !api_key_.empty(); }

  // Set batch size (default: 128, max: 128)
  void SetBatchSize(int batch_size);

  // Set API endpoint (default: https://api.voyageai.com/v1/embeddings)
  void SetEndpoint(const std::string& endpoint);

  // Set input type for better performance
  // Options: "query", "document"
  void SetInputType(const std::string& input_type);

 private:
  std::string api_key_;
  std::string model_;
  std::string endpoint_;
  std::string input_type_;  // "query" or "document"
  int batch_size_;
  int dimension_;
  std::shared_ptr<spdlog::logger> logger_;

  // Make API request
  std::vector<std::vector<float>> MakeRequest(
      const std::vector<std::string>& texts);

  // Determine dimension based on model
  int GetModelDimension(const std::string& model);
};

}  // namespace quantclaw
