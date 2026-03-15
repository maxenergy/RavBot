// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "quantclaw/core/embedding_provider.hpp"

#include <spdlog/spdlog.h>
#include <memory>
#include <string>

namespace quantclaw {

// Ollama Embedding Provider
// Supports local embedding models via Ollama API
class OllamaEmbeddingProvider : public EmbeddingProvider {
 public:
  OllamaEmbeddingProvider(const std::string& model = "nomic-embed-text",
                         const std::string& endpoint = "http://localhost:11434",
                         std::shared_ptr<spdlog::logger> logger = nullptr);
  ~OllamaEmbeddingProvider() override = default;

  std::vector<float> Embed(const std::string& text) override;

  std::vector<std::vector<float>> EmbedBatch(
      const std::vector<std::string>& texts) override;

  int GetDimension() const override;

  std::string GetName() const override { return "ollama"; }

  std::string GetModel() const override { return model_; }

  bool IsAvailable() const override;

  // Set API endpoint
  void SetEndpoint(const std::string& endpoint);

  // Set model
  void SetModel(const std::string& model);

  // Set request timeout (seconds)
  void SetTimeout(int timeout_seconds);

 private:
  std::string model_;
  std::string endpoint_;
  int timeout_;
  mutable int dimension_;  // Cached dimension
  std::shared_ptr<spdlog::logger> logger_;

  // Make API request
  std::vector<std::vector<float>> MakeRequest(
      const std::vector<std::string>& texts);

  // Detect dimension from first embedding
  void DetectDimension();

  // Check if Ollama service is available
  bool CheckAvailability() const;
};

}  // namespace quantclaw
