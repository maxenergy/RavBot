// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/mock_embedding_provider.hpp"

#include <cmath>
#include <random>

namespace quantclaw {

MockEmbeddingProvider::MockEmbeddingProvider(int dimension)
    : dimension_(dimension) {}

std::vector<float> MockEmbeddingProvider::Embed(const std::string& text) {
  return GenerateVector(text);
}

std::vector<std::vector<float>> MockEmbeddingProvider::EmbedBatch(
    const std::vector<std::string>& texts) {
  std::vector<std::vector<float>> results;
  results.reserve(texts.size());
  for (const auto& text : texts) {
    results.push_back(GenerateVector(text));
  }
  return results;
}

std::vector<float> MockEmbeddingProvider::GenerateVector(const std::string& text) {
  // Use text hash as seed for deterministic generation
  size_t seed = hasher_(text);
  std::mt19937 rng(static_cast<unsigned int>(seed));
  std::normal_distribution<float> dist(0.0f, 1.0f);

  // Generate random vector
  std::vector<float> vector(dimension_);
  for (int i = 0; i < dimension_; ++i) {
    vector[i] = dist(rng);
  }

  // Normalize to unit length (for cosine similarity)
  float norm = 0.0f;
  for (float v : vector) {
    norm += v * v;
  }
  norm = std::sqrt(norm);

  if (norm > 0.0f) {
    for (float& v : vector) {
      v /= norm;
    }
  }

  return vector;
}

}  // namespace quantclaw
