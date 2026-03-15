// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/embedding_provider.hpp"
#include "quantclaw/core/mock_embedding_provider.hpp"

#include <gtest/gtest.h>
#include <cmath>

using namespace quantclaw;

// Test MockEmbeddingProvider
TEST(MockEmbeddingProviderTest, BasicEmbedding) {
  MockEmbeddingProvider provider(384);

  EXPECT_EQ(provider.GetDimension(), 384);
  EXPECT_EQ(provider.GetName(), "mock");
  EXPECT_EQ(provider.GetModel(), "mock-v1");
  EXPECT_TRUE(provider.IsAvailable());

  auto vector = provider.Embed("Hello, world!");
  EXPECT_EQ(vector.size(), 384);

  // Check normalization (unit length)
  float norm = 0.0f;
  for (float v : vector) {
    norm += v * v;
  }
  EXPECT_NEAR(std::sqrt(norm), 1.0f, 1e-5f);
}

TEST(MockEmbeddingProviderTest, DeterministicOutput) {
  MockEmbeddingProvider provider(128);

  auto vector1 = provider.Embed("test text");
  auto vector2 = provider.Embed("test text");

  ASSERT_EQ(vector1.size(), vector2.size());
  for (size_t i = 0; i < vector1.size(); ++i) {
    EXPECT_FLOAT_EQ(vector1[i], vector2[i]);
  }
}

TEST(MockEmbeddingProviderTest, DifferentTexts) {
  MockEmbeddingProvider provider(256);

  auto vector1 = provider.Embed("text one");
  auto vector2 = provider.Embed("text two");

  ASSERT_EQ(vector1.size(), vector2.size());

  // Vectors should be different
  bool different = false;
  for (size_t i = 0; i < vector1.size(); ++i) {
    if (std::abs(vector1[i] - vector2[i]) > 1e-6f) {
      different = true;
      break;
    }
  }
  EXPECT_TRUE(different);
}

TEST(MockEmbeddingProviderTest, BatchEmbedding) {
  MockEmbeddingProvider provider(128);

  std::vector<std::string> texts = {"text1", "text2", "text3"};
  auto vectors = provider.EmbedBatch(texts);

  ASSERT_EQ(vectors.size(), 3);
  for (const auto& vector : vectors) {
    EXPECT_EQ(vector.size(), 128);
  }

  // Batch results should match individual results
  for (size_t i = 0; i < texts.size(); ++i) {
    auto individual = provider.Embed(texts[i]);
    ASSERT_EQ(vectors[i].size(), individual.size());
    for (size_t j = 0; j < individual.size(); ++j) {
      EXPECT_FLOAT_EQ(vectors[i][j], individual[j]);
    }
  }
}

TEST(MockEmbeddingProviderTest, EmptyText) {
  MockEmbeddingProvider provider(64);

  auto vector = provider.Embed("");
  EXPECT_EQ(vector.size(), 64);

  // Should still be normalized
  float norm = 0.0f;
  for (float v : vector) {
    norm += v * v;
  }
  EXPECT_NEAR(std::sqrt(norm), 1.0f, 1e-5f);
}

// Test EmbeddingProviderRegistry
TEST(EmbeddingProviderRegistryTest, RegisterAndGet) {
  EmbeddingProviderRegistry registry;

  auto provider = std::make_shared<MockEmbeddingProvider>(384);
  registry.RegisterProvider("mock", provider);

  EXPECT_TRUE(registry.HasProvider("mock"));
  EXPECT_FALSE(registry.HasProvider("nonexistent"));

  auto retrieved = registry.GetProvider("mock");
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->GetName(), "mock");
}

TEST(EmbeddingProviderRegistryTest, DefaultProvider) {
  EmbeddingProviderRegistry registry;

  auto provider1 = std::make_shared<MockEmbeddingProvider>(128);
  auto provider2 = std::make_shared<MockEmbeddingProvider>(256);

  registry.RegisterProvider("provider1", provider1);
  registry.RegisterProvider("provider2", provider2);

  // First registered provider should be default
  auto default_provider = registry.GetDefaultProvider();
  ASSERT_NE(default_provider, nullptr);
  EXPECT_EQ(default_provider->GetDimension(), 128);

  // Change default
  registry.SetDefaultProvider("provider2");
  default_provider = registry.GetDefaultProvider();
  ASSERT_NE(default_provider, nullptr);
  EXPECT_EQ(default_provider->GetDimension(), 256);
}

TEST(EmbeddingProviderRegistryTest, ListProviders) {
  EmbeddingProviderRegistry registry;

  registry.RegisterProvider("mock1", std::make_shared<MockEmbeddingProvider>(128));
  registry.RegisterProvider("mock2", std::make_shared<MockEmbeddingProvider>(256));
  registry.RegisterProvider("mock3", std::make_shared<MockEmbeddingProvider>(384));

  auto providers = registry.ListProviders();
  ASSERT_EQ(providers.size(), 3);

  // Should be sorted
  EXPECT_EQ(providers[0], "mock1");
  EXPECT_EQ(providers[1], "mock2");
  EXPECT_EQ(providers[2], "mock3");
}

TEST(EmbeddingProviderRegistryTest, UnregisterProvider) {
  EmbeddingProviderRegistry registry;

  registry.RegisterProvider("mock1", std::make_shared<MockEmbeddingProvider>(128));
  registry.RegisterProvider("mock2", std::make_shared<MockEmbeddingProvider>(256));

  EXPECT_TRUE(registry.HasProvider("mock1"));
  EXPECT_TRUE(registry.HasProvider("mock2"));

  registry.UnregisterProvider("mock1");

  EXPECT_FALSE(registry.HasProvider("mock1"));
  EXPECT_TRUE(registry.HasProvider("mock2"));

  // Default should switch to remaining provider
  auto default_provider = registry.GetDefaultProvider();
  ASSERT_NE(default_provider, nullptr);
  EXPECT_EQ(default_provider->GetDimension(), 256);
}

TEST(EmbeddingProviderRegistryTest, GetNonexistentProvider) {
  EmbeddingProviderRegistry registry;

  auto provider = registry.GetProvider("nonexistent");
  EXPECT_EQ(provider, nullptr);
}

TEST(EmbeddingProviderRegistryTest, EmptyRegistry) {
  EmbeddingProviderRegistry registry;

  EXPECT_EQ(registry.ListProviders().size(), 0);
  EXPECT_EQ(registry.GetDefaultProvider(), nullptr);
}
