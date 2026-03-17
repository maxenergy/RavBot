// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ravbot/core/embedding_provider.hpp"

#include <functional>

namespace ravbot {

// Mock embedding provider for testing
// Generates deterministic vectors based on text hash
class MockEmbeddingProvider : public EmbeddingProvider {
 public:
  explicit MockEmbeddingProvider(int dimension = 384);
  ~MockEmbeddingProvider() override = default;

  std::vector<float> Embed(const std::string& text) override;

  std::vector<std::vector<float>> EmbedBatch(
      const std::vector<std::string>& texts) override;

  int GetDimension() const override { return dimension_; }

  std::string GetName() const override { return "mock"; }

  std::string GetModel() const override { return "mock-v1"; }

 private:
  int dimension_;
  std::hash<std::string> hasher_;

  // Generate a deterministic vector from text
  std::vector<float> GenerateVector(const std::string& text);
};

}  // namespace ravbot
