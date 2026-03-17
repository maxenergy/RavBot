// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ravbot/core/embedding_provider.hpp"

#include <spdlog/spdlog.h>
#include <memory>
#include <string>

namespace ravbot {

// OpenAI Embedding Provider
// Supports text-embedding-3-small and text-embedding-3-large models
class OpenAIEmbeddingProvider : public EmbeddingProvider {
 public:
  OpenAIEmbeddingProvider(const std::string& api_key,
                         const std::string& model = "text-embedding-3-small",
                         std::shared_ptr<spdlog::logger> logger = nullptr);
  ~OpenAIEmbeddingProvider() override = default;

  std::vector<float> Embed(const std::string& text) override;

  std::vector<std::vector<float>> EmbedBatch(
      const std::vector<std::string>& texts) override;

  int GetDimension() const override;

  std::string GetName() const override { return "openai"; }

  std::string GetModel() const override { return model_; }

  bool IsAvailable() const override { return !api_key_.empty(); }

  // Set batch size (default: 100, max: 2048)
  void SetBatchSize(int batch_size);

  // Set API endpoint (default: https://api.openai.com/v1/embeddings)
  void SetEndpoint(const std::string& endpoint);

 private:
  std::string api_key_;
  std::string model_;
  std::string endpoint_;
  int batch_size_;
  int dimension_;
  std::shared_ptr<spdlog::logger> logger_;

  // Make API request
  std::vector<std::vector<float>> MakeRequest(
      const std::vector<std::string>& texts);

  // Determine dimension based on model
  int GetModelDimension(const std::string& model);
};

}  // namespace ravbot
