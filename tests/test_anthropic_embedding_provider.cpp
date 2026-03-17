// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/core/anthropic_embedding_provider.hpp"

#include <gtest/gtest.h>
#include <cmath>
#include <cstdlib>

using namespace ravbot;

// Test AnthropicEmbeddingProvider basic functionality
// Note: These tests require VOYAGE_API_KEY environment variable to be set
// If not set, tests will be skipped

class AnthropicEmbeddingProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* api_key = std::getenv("VOYAGE_API_KEY");
    if (api_key && std::string(api_key).length() > 0) {
      api_key_ = api_key;
      has_api_key_ = true;
    } else {
      has_api_key_ = false;
      GTEST_SKIP() << "VOYAGE_API_KEY not set, skipping Anthropic/Voyage tests";
    }
  }

  std::string api_key_;
  bool has_api_key_ = false;
};

TEST_F(AnthropicEmbeddingProviderTest, BasicProperties) {
  if (!has_api_key_) return;

  AnthropicEmbeddingProvider provider(api_key_, "voyage-3");

  EXPECT_EQ(provider.GetName(), "anthropic");
  EXPECT_EQ(provider.GetModel(), "voyage-3");
  EXPECT_EQ(provider.GetDimension(), 1024);
  EXPECT_TRUE(provider.IsAvailable());
}

TEST_F(AnthropicEmbeddingProviderTest, LiteModelDimension) {
  if (!has_api_key_) return;

  AnthropicEmbeddingProvider provider(api_key_, "voyage-3-lite");

  EXPECT_EQ(provider.GetDimension(), 512);
}

TEST_F(AnthropicEmbeddingProviderTest, CodeModelDimension) {
  if (!has_api_key_) return;

  AnthropicEmbeddingProvider provider(api_key_, "voyage-code-3");

  EXPECT_EQ(provider.GetDimension(), 1024);
}

TEST_F(AnthropicEmbeddingProviderTest, SingleEmbedding) {
  if (!has_api_key_) return;

  AnthropicEmbeddingProvider provider(api_key_, "voyage-3");

  auto vector = provider.Embed("Hello, world!");

  EXPECT_EQ(static_cast<int>(vector.size()), provider.GetDimension());

  // Check that vector is not all zeros
  float sum = 0.0f;
  for (float v : vector) {
    sum += std::abs(v);
  }
  EXPECT_GT(sum, 0.0f);
}

TEST_F(AnthropicEmbeddingProviderTest, BatchEmbedding) {
  if (!has_api_key_) return;

  AnthropicEmbeddingProvider provider(api_key_, "voyage-3");

  std::vector<std::string> texts = {
    "First text",
    "Second text",
    "Third text"
  };

  auto vectors = provider.EmbedBatch(texts);

  ASSERT_EQ(vectors.size(), 3);
  for (const auto& vector : vectors) {
    EXPECT_EQ(static_cast<int>(vector.size()), provider.GetDimension());
  }
}

TEST_F(AnthropicEmbeddingProviderTest, EmptyText) {
  if (!has_api_key_) return;

  AnthropicEmbeddingProvider provider(api_key_, "voyage-3");

  auto vector = provider.Embed("");

  EXPECT_EQ(static_cast<int>(vector.size()), provider.GetDimension());
}

TEST_F(AnthropicEmbeddingProviderTest, SimilarTexts) {
  if (!has_api_key_) return;

  AnthropicEmbeddingProvider provider(api_key_, "voyage-3");

  auto vec1 = provider.Embed("The cat sits on the mat");
  auto vec2 = provider.Embed("A cat is sitting on a mat");
  auto vec3 = provider.Embed("The weather is nice today");

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
  EXPECT_GT(sim_12, 0.8f);  // Similar texts should be quite similar
}

TEST_F(AnthropicEmbeddingProviderTest, InputTypeConfiguration) {
  if (!has_api_key_) return;

  AnthropicEmbeddingProvider provider(api_key_, "voyage-3");

  provider.SetInputType("query");
  provider.SetInputType("document");

  EXPECT_THROW(provider.SetInputType("invalid"), std::invalid_argument);
}

TEST_F(AnthropicEmbeddingProviderTest, BatchSizeConfiguration) {
  if (!has_api_key_) return;

  AnthropicEmbeddingProvider provider(api_key_, "voyage-3");

  provider.SetBatchSize(64);
  // No exception should be thrown

  EXPECT_THROW(provider.SetBatchSize(0), std::invalid_argument);
  EXPECT_THROW(provider.SetBatchSize(-1), std::invalid_argument);
  EXPECT_THROW(provider.SetBatchSize(200), std::invalid_argument);
}

TEST_F(AnthropicEmbeddingProviderTest, CustomEndpoint) {
  if (!has_api_key_) return;

  AnthropicEmbeddingProvider provider(api_key_, "voyage-3");

  provider.SetEndpoint("https://api.voyageai.com/v1/embeddings");
  // No exception should be thrown
}

// Test without API key
TEST(AnthropicEmbeddingProviderNoKeyTest, EmptyAPIKey) {
  AnthropicEmbeddingProvider provider("", "voyage-3");

  EXPECT_FALSE(provider.IsAvailable());
  EXPECT_EQ(provider.GetName(), "anthropic");
  EXPECT_EQ(provider.GetDimension(), 1024);
}

TEST(AnthropicEmbeddingProviderNoKeyTest, ModelDimensions) {
  AnthropicEmbeddingProvider provider1("fake-key", "voyage-3");
  EXPECT_EQ(provider1.GetDimension(), 1024);

  AnthropicEmbeddingProvider provider2("fake-key", "voyage-3-lite");
  EXPECT_EQ(provider2.GetDimension(), 512);

  AnthropicEmbeddingProvider provider3("fake-key", "voyage-code-3");
  EXPECT_EQ(provider3.GetDimension(), 1024);

  AnthropicEmbeddingProvider provider4("fake-key", "voyage-2");
  EXPECT_EQ(provider4.GetDimension(), 1024);

  AnthropicEmbeddingProvider provider5("fake-key", "voyage-large-2");
  EXPECT_EQ(provider5.GetDimension(), 1536);

  AnthropicEmbeddingProvider provider6("fake-key", "voyage-code-2");
  EXPECT_EQ(provider6.GetDimension(), 1536);

  // Unknown model defaults to 1024
  AnthropicEmbeddingProvider provider7("fake-key", "unknown-model");
  EXPECT_EQ(provider7.GetDimension(), 1024);
}
