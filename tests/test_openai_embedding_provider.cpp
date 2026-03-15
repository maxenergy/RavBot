// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/openai_embedding_provider.hpp"

#include <gtest/gtest.h>
#include <cmath>
#include <cstdlib>

using namespace quantclaw;

// Test OpenAIEmbeddingProvider basic functionality
// Note: These tests require OPENAI_API_KEY environment variable to be set
// If not set, tests will be skipped

class OpenAIEmbeddingProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* api_key = std::getenv("OPENAI_API_KEY");
    if (api_key && std::string(api_key).length() > 0) {
      api_key_ = api_key;
      has_api_key_ = true;
    } else {
      has_api_key_ = false;
      GTEST_SKIP() << "OPENAI_API_KEY not set, skipping OpenAI tests";
    }
  }

  std::string api_key_;
  bool has_api_key_ = false;
};

TEST_F(OpenAIEmbeddingProviderTest, BasicProperties) {
  if (!has_api_key_) return;

  OpenAIEmbeddingProvider provider(api_key_, "text-embedding-3-small");

  EXPECT_EQ(provider.GetName(), "openai");
  EXPECT_EQ(provider.GetModel(), "text-embedding-3-small");
  EXPECT_EQ(provider.GetDimension(), 1536);
  EXPECT_TRUE(provider.IsAvailable());
}

TEST_F(OpenAIEmbeddingProviderTest, LargeModelDimension) {
  if (!has_api_key_) return;

  OpenAIEmbeddingProvider provider(api_key_, "text-embedding-3-large");

  EXPECT_EQ(provider.GetDimension(), 3072);
}

TEST_F(OpenAIEmbeddingProviderTest, SingleEmbedding) {
  if (!has_api_key_) return;

  OpenAIEmbeddingProvider provider(api_key_, "text-embedding-3-small");

  auto vector = provider.Embed("Hello, world!");

  EXPECT_EQ(static_cast<int>(vector.size()), provider.GetDimension());

  // Check that vector is not all zeros
  float sum = 0.0f;
  for (float v : vector) {
    sum += std::abs(v);
  }
  EXPECT_GT(sum, 0.0f);
}

TEST_F(OpenAIEmbeddingProviderTest, BatchEmbedding) {
  if (!has_api_key_) return;

  OpenAIEmbeddingProvider provider(api_key_, "text-embedding-3-small");

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

TEST_F(OpenAIEmbeddingProviderTest, EmptyText) {
  if (!has_api_key_) return;

  OpenAIEmbeddingProvider provider(api_key_, "text-embedding-3-small");

  auto vector = provider.Embed("");

  EXPECT_EQ(static_cast<int>(vector.size()), provider.GetDimension());
}

TEST_F(OpenAIEmbeddingProviderTest, SimilarTexts) {
  if (!has_api_key_) return;

  OpenAIEmbeddingProvider provider(api_key_, "text-embedding-3-small");

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

TEST_F(OpenAIEmbeddingProviderTest, BatchSizeConfiguration) {
  if (!has_api_key_) return;

  OpenAIEmbeddingProvider provider(api_key_, "text-embedding-3-small");

  provider.SetBatchSize(50);
  // No exception should be thrown

  EXPECT_THROW(provider.SetBatchSize(0), std::invalid_argument);
  EXPECT_THROW(provider.SetBatchSize(-1), std::invalid_argument);
  EXPECT_THROW(provider.SetBatchSize(3000), std::invalid_argument);
}

TEST_F(OpenAIEmbeddingProviderTest, CustomEndpoint) {
  if (!has_api_key_) return;

  OpenAIEmbeddingProvider provider(api_key_, "text-embedding-3-small");

  provider.SetEndpoint("https://api.openai.com/v1/embeddings");
  // No exception should be thrown
}

// Test without API key
TEST(OpenAIEmbeddingProviderNoKeyTest, EmptyAPIKey) {
  OpenAIEmbeddingProvider provider("", "text-embedding-3-small");

  EXPECT_FALSE(provider.IsAvailable());
  EXPECT_EQ(provider.GetName(), "openai");
  EXPECT_EQ(provider.GetDimension(), 1536);
}

TEST(OpenAIEmbeddingProviderNoKeyTest, ModelDimensions) {
  OpenAIEmbeddingProvider provider1("fake-key", "text-embedding-3-small");
  EXPECT_EQ(provider1.GetDimension(), 1536);

  OpenAIEmbeddingProvider provider2("fake-key", "text-embedding-3-large");
  EXPECT_EQ(provider2.GetDimension(), 3072);

  OpenAIEmbeddingProvider provider3("fake-key", "text-embedding-ada-002");
  EXPECT_EQ(provider3.GetDimension(), 1536);

  // Unknown model defaults to 1536
  OpenAIEmbeddingProvider provider4("fake-key", "unknown-model");
  EXPECT_EQ(provider4.GetDimension(), 1536);
}
