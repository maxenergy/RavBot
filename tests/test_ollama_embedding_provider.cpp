// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/ollama_embedding_provider.hpp"

#include <gtest/gtest.h>
#include <cmath>
#include <cstdlib>

using namespace quantclaw;

// Test OllamaEmbeddingProvider basic functionality
// Note: These tests require Ollama service running locally
// If not available, tests will be skipped

class OllamaEmbeddingProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    provider_ = std::make_shared<OllamaEmbeddingProvider>();
    has_ollama_ = provider_->IsAvailable();

    if (!has_ollama_) {
      GTEST_SKIP() << "Ollama service not available, skipping tests";
    }
  }

  std::shared_ptr<OllamaEmbeddingProvider> provider_;
  bool has_ollama_ = false;
};

TEST_F(OllamaEmbeddingProviderTest, BasicProperties) {
  if (!has_ollama_) return;

  EXPECT_EQ(provider_->GetName(), "ollama");
  EXPECT_EQ(provider_->GetModel(), "nomic-embed-text");
  EXPECT_TRUE(provider_->IsAvailable());
}

TEST_F(OllamaEmbeddingProviderTest, SingleEmbedding) {
  if (!has_ollama_) return;

  auto vector = provider_->Embed("Hello, world!");

  EXPECT_GT(vector.size(), 0);
  EXPECT_EQ(static_cast<int>(vector.size()), provider_->GetDimension());

  // Check that vector is not all zeros
  float sum = 0.0f;
  for (float v : vector) {
    sum += std::abs(v);
  }
  EXPECT_GT(sum, 0.0f);
}

TEST_F(OllamaEmbeddingProviderTest, BatchEmbedding) {
  if (!has_ollama_) return;

  std::vector<std::string> texts = {
    "First text",
    "Second text",
    "Third text"
  };

  auto vectors = provider_->EmbedBatch(texts);

  ASSERT_EQ(vectors.size(), 3);
  for (const auto& vector : vectors) {
    EXPECT_EQ(static_cast<int>(vector.size()), provider_->GetDimension());
  }
}

TEST_F(OllamaEmbeddingProviderTest, EmptyText) {
  if (!has_ollama_) return;

  auto vector = provider_->Embed("");

  // Ollama returns empty vector for empty text, which is reasonable behavior
  EXPECT_TRUE(vector.empty() || static_cast<int>(vector.size()) == provider_->GetDimension());
}

TEST_F(OllamaEmbeddingProviderTest, SimilarTexts) {
  if (!has_ollama_) return;

  auto vec1 = provider_->Embed("The cat sits on the mat");
  auto vec2 = provider_->Embed("A cat is sitting on a mat");
  auto vec3 = provider_->Embed("The weather is nice today");

  // Compute cosine similarity
  auto cosine_similarity = [](const std::vector<float>& a, const std::vector<float>& b) {
    float dot = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
      dot += a[i] * b[i];
      norm_a += a[i] * a[i];
      norm_b += b[i] * b[i];
    }
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
  };

  float sim_12 = cosine_similarity(vec1, vec2);
  float sim_13 = cosine_similarity(vec1, vec3);

  // Similar texts should have higher similarity
  EXPECT_GT(sim_12, sim_13);
}

TEST_F(OllamaEmbeddingProviderTest, CustomEndpoint) {
  if (!has_ollama_) return;

  provider_->SetEndpoint("http://localhost:11434");
  // No exception should be thrown
}

TEST_F(OllamaEmbeddingProviderTest, CustomModel) {
  if (!has_ollama_) return;

  provider_->SetModel("nomic-embed-text");
  EXPECT_EQ(provider_->GetModel(), "nomic-embed-text");
}

TEST_F(OllamaEmbeddingProviderTest, Timeout) {
  if (!has_ollama_) return;

  provider_->SetTimeout(60);
  // No exception should be thrown
}

// Test without Ollama service
TEST(OllamaEmbeddingProviderNoServiceTest, BasicProperties) {
  OllamaEmbeddingProvider provider("nomic-embed-text", "http://localhost:11434");

  EXPECT_EQ(provider.GetName(), "ollama");
  EXPECT_EQ(provider.GetModel(), "nomic-embed-text");
}

TEST(OllamaEmbeddingProviderNoServiceTest, CustomEndpoint) {
  OllamaEmbeddingProvider provider("nomic-embed-text", "http://custom:8080");

  EXPECT_EQ(provider.GetName(), "ollama");
}
