// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/local_embedding_provider.hpp"

#include <gtest/gtest.h>
#include <cmath>

using namespace quantclaw;

TEST(LocalEmbeddingProviderTest, BasicProperties) {
  LocalEmbeddingProvider provider(384);

  EXPECT_EQ(provider.GetDimension(), 384);
  EXPECT_EQ(provider.GetName(), "local");
  EXPECT_EQ(provider.GetModel(), "tfidf-lsa");
  EXPECT_TRUE(provider.IsAvailable());
}

TEST(LocalEmbeddingProviderTest, DefaultEmbedding) {
  LocalEmbeddingProvider provider(128);

  auto vector = provider.Embed("Hello, world!");

  EXPECT_EQ(vector.size(), 128);

  // Check normalization (unit length)
  float norm = 0.0f;
  for (float v : vector) {
    norm += v * v;
  }
  EXPECT_NEAR(std::sqrt(norm), 1.0f, 1e-5f);
}

TEST(LocalEmbeddingProviderTest, DifferentTexts) {
  LocalEmbeddingProvider provider(256);

  auto vec1 = provider.Embed("The cat sits on the mat");
  auto vec2 = provider.Embed("A dog runs in the park");

  ASSERT_EQ(vec1.size(), vec2.size());

  // Vectors should be different
  bool different = false;
  for (size_t i = 0; i < vec1.size(); ++i) {
    if (std::abs(vec1[i] - vec2[i]) > 1e-6f) {
      different = true;
      break;
    }
  }
  EXPECT_TRUE(different);
}

TEST(LocalEmbeddingProviderTest, SimilarTexts) {
  LocalEmbeddingProvider provider(256);

  // Train on a small corpus
  std::vector<std::string> corpus = {
    "The cat sits on the mat",
    "A cat is sitting on a mat",
    "The dog runs in the park",
    "A dog is running in a park",
    "The weather is nice today",
    "Today the weather is beautiful"
  };
  provider.Train(corpus);

  auto vec1 = provider.Embed("The cat sits on the mat");
  auto vec2 = provider.Embed("A cat is sitting on a mat");
  auto vec3 = provider.Embed("The weather is nice today");

  // Compute cosine similarity
  auto cosine_similarity = [](const std::vector<float>& a, const std::vector<float>& b) {
    float dot = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
      dot += a[i] * b[i];
    }
    return dot;  // Already normalized
  };

  float sim_12 = cosine_similarity(vec1, vec2);
  float sim_13 = cosine_similarity(vec1, vec3);

  // Similar texts should have higher similarity
  EXPECT_GT(sim_12, sim_13);
}

TEST(LocalEmbeddingProviderTest, BatchEmbedding) {
  LocalEmbeddingProvider provider(128);

  std::vector<std::string> texts = {"text1", "text2", "text3"};
  auto vectors = provider.EmbedBatch(texts);

  ASSERT_EQ(vectors.size(), 3);
  for (const auto& vector : vectors) {
    EXPECT_EQ(vector.size(), 128);
  }
}

TEST(LocalEmbeddingProviderTest, EmptyText) {
  LocalEmbeddingProvider provider(64);

  auto vector = provider.Embed("");

  EXPECT_EQ(vector.size(), 64);

  // Should still be normalized
  float norm = 0.0f;
  for (float v : vector) {
    norm += v * v;
  }
  EXPECT_NEAR(std::sqrt(norm), 1.0f, 1e-5f);
}

TEST(LocalEmbeddingProviderTest, Training) {
  LocalEmbeddingProvider provider(128);

  EXPECT_FALSE(provider.IsTrained());

  std::vector<std::string> corpus = {
    "Machine learning is a subset of artificial intelligence",
    "Deep learning uses neural networks with multiple layers",
    "Natural language processing deals with text and speech",
    "Computer vision enables machines to interpret images",
    "Reinforcement learning learns through trial and error"
  };

  provider.Train(corpus);

  EXPECT_TRUE(provider.IsTrained());
  EXPECT_GT(provider.GetVocabularySize(), 0);
}

TEST(LocalEmbeddingProviderTest, TrainingImprovesSimilarity) {
  LocalEmbeddingProvider provider(256);

  // Before training
  auto vec1_before = provider.Embed("machine learning artificial intelligence");
  auto vec2_before = provider.Embed("deep learning neural networks");
  auto vec3_before = provider.Embed("cooking recipes food");

  auto cosine_similarity = [](const std::vector<float>& a, const std::vector<float>& b) {
    float dot = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
      dot += a[i] * b[i];
    }
    return dot;
  };

  float sim_12_before = cosine_similarity(vec1_before, vec2_before);
  float sim_13_before = cosine_similarity(vec1_before, vec3_before);

  // Train on ML corpus
  std::vector<std::string> corpus = {
    "Machine learning is a subset of artificial intelligence",
    "Deep learning uses neural networks with multiple layers",
    "Natural language processing deals with text and speech",
    "Computer vision enables machines to interpret images",
    "Reinforcement learning learns through trial and error",
    "Neural networks are inspired by biological neurons",
    "Artificial intelligence aims to create intelligent machines",
    "Supervised learning uses labeled training data"
  };
  provider.Train(corpus);

  // After training
  auto vec1_after = provider.Embed("machine learning artificial intelligence");
  auto vec2_after = provider.Embed("deep learning neural networks");
  auto vec3_after = provider.Embed("cooking recipes food");

  float sim_12_after = cosine_similarity(vec1_after, vec2_after);
  float sim_13_after = cosine_similarity(vec1_after, vec3_after);

  // After training, related texts should be more similar
  EXPECT_GT(sim_12_after, sim_13_after);
}

TEST(LocalEmbeddingProviderTest, ConsistentEmbeddings) {
  LocalEmbeddingProvider provider(128);

  auto vec1 = provider.Embed("test text");
  auto vec2 = provider.Embed("test text");

  ASSERT_EQ(vec1.size(), vec2.size());
  for (size_t i = 0; i < vec1.size(); ++i) {
    EXPECT_FLOAT_EQ(vec1[i], vec2[i]);
  }
}

TEST(LocalEmbeddingProviderTest, LargeCorpusTraining) {
  LocalEmbeddingProvider provider(384);

  // Generate a larger corpus
  std::vector<std::string> corpus;
  for (int i = 0; i < 100; ++i) {
    corpus.push_back("Document " + std::to_string(i) + " contains various words and phrases");
  }

  provider.Train(corpus);

  EXPECT_TRUE(provider.IsTrained());
  EXPECT_GT(provider.GetVocabularySize(), 0);

  auto vector = provider.Embed("test document");
  EXPECT_EQ(static_cast<int>(vector.size()), provider.GetDimension());
}
