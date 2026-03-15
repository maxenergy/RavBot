// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/embedding_manager.hpp"
#include "quantclaw/core/local_embedding_provider.hpp"
#include "quantclaw/core/mock_embedding_provider.hpp"

#include <gtest/gtest.h>
#include <memory>

using namespace quantclaw;

class EmbeddingManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create logger
    logger_ = spdlog::default_logger();

    // Create providers
    auto local_provider = std::make_shared<LocalEmbeddingProvider>(128, logger_);
    auto mock_provider = std::make_shared<MockEmbeddingProvider>(128);

    // Create registry
    registry_ = std::make_shared<EmbeddingProviderRegistry>();
    registry_->RegisterProvider("local", local_provider);
    registry_->RegisterProvider("mock", mock_provider);
    registry_->SetDefaultProvider("local");

    // Create vector database (disable HNSW for faster tests)
    VectorDatabaseConfig config;
    config.use_hnsw = false;
    vector_db_ = std::make_shared<VectorDatabase>(":memory:", logger_, config);
    vector_db_->Initialize();

    // Create manager
    manager_ = std::make_shared<EmbeddingManager>(registry_, vector_db_, logger_);
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<EmbeddingProviderRegistry> registry_;
  std::shared_ptr<VectorDatabase> vector_db_;
  std::shared_ptr<EmbeddingManager> manager_;
};

TEST_F(EmbeddingManagerTest, BasicIndexing) {
  EXPECT_TRUE(manager_->IndexText("doc1", "Hello, world!"));
  EXPECT_TRUE(manager_->IndexText("doc2", "Machine learning is great"));

  auto results = manager_->SearchText("Hello", 10);
  EXPECT_GT(results.size(), 0);
}

TEST_F(EmbeddingManagerTest, BatchIndexing) {
  std::vector<std::string> ids = {"doc1", "doc2", "doc3"};
  std::vector<std::string> texts = {
    "First document",
    "Second document",
    "Third document"
  };

  EXPECT_TRUE(manager_->IndexTextBatch(ids, texts));

  auto results = manager_->SearchText("document", 10);
  EXPECT_EQ(results.size(), 3);
}

TEST_F(EmbeddingManagerTest, BatchIndexingWithMetadata) {
  std::vector<std::string> ids = {"doc1", "doc2"};
  std::vector<std::string> texts = {"Text 1", "Text 2"};
  std::vector<std::string> metadata = {"meta1", "meta2"};

  EXPECT_TRUE(manager_->IndexTextBatch(ids, texts, metadata));

  auto results = manager_->SearchText("Text", 10);
  EXPECT_EQ(results.size(), 2);
}

TEST_F(EmbeddingManagerTest, SearchText) {
  manager_->IndexText("doc1", "Machine learning is a subset of AI");
  manager_->IndexText("doc2", "Deep learning uses neural networks");
  manager_->IndexText("doc3", "Cooking recipes for dinner");

  auto results = manager_->SearchText("artificial intelligence", 2);
  EXPECT_LE(results.size(), 2);
}

TEST_F(EmbeddingManagerTest, GetEmbedding) {
  auto embedding = manager_->GetEmbedding("test text");
  EXPECT_EQ(embedding.size(), 128);
}

TEST_F(EmbeddingManagerTest, GetEmbeddingBatch) {
  std::vector<std::string> texts = {"text1", "text2", "text3"};
  auto embeddings = manager_->GetEmbeddingBatch(texts);

  EXPECT_EQ(embeddings.size(), 3);
  for (const auto& emb : embeddings) {
    EXPECT_EQ(emb.size(), 128);
  }
}

TEST_F(EmbeddingManagerTest, CacheEnabled) {
  manager_->SetCacheEnabled(true);

  // First call - cache miss
  auto emb1 = manager_->GetEmbedding("test text");

  // Second call - cache hit
  auto emb2 = manager_->GetEmbedding("test text");

  EXPECT_EQ(emb1.size(), emb2.size());
  for (size_t i = 0; i < emb1.size(); ++i) {
    EXPECT_FLOAT_EQ(emb1[i], emb2[i]);
  }

  auto stats = manager_->GetCacheStats();
  EXPECT_EQ(stats.hits, 1);
  EXPECT_EQ(stats.misses, 1);
  EXPECT_EQ(stats.size, 1);
}

TEST_F(EmbeddingManagerTest, CacheDisabled) {
  manager_->SetCacheEnabled(false);

  manager_->GetEmbedding("test text");
  manager_->GetEmbedding("test text");

  auto stats = manager_->GetCacheStats();
  EXPECT_EQ(stats.hits, 0);
  EXPECT_EQ(stats.misses, 0);
  EXPECT_EQ(stats.size, 0);
}

TEST_F(EmbeddingManagerTest, ClearCache) {
  manager_->SetCacheEnabled(true);

  manager_->GetEmbedding("text1");
  manager_->GetEmbedding("text2");

  auto stats1 = manager_->GetCacheStats();
  EXPECT_EQ(stats1.size, 2);

  manager_->ClearCache();

  auto stats2 = manager_->GetCacheStats();
  EXPECT_EQ(stats2.size, 0);
  EXPECT_EQ(stats2.hits, 0);
  EXPECT_EQ(stats2.misses, 0);
}

TEST_F(EmbeddingManagerTest, BatchCaching) {
  manager_->SetCacheEnabled(true);

  std::vector<std::string> texts = {"text1", "text2", "text3"};

  // First batch - all cache misses
  auto emb1 = manager_->GetEmbeddingBatch(texts);

  // Second batch - all cache hits
  auto emb2 = manager_->GetEmbeddingBatch(texts);

  EXPECT_EQ(emb1.size(), emb2.size());

  auto stats = manager_->GetCacheStats();
  EXPECT_EQ(stats.hits, 3);
  EXPECT_EQ(stats.misses, 3);
  EXPECT_EQ(stats.size, 3);
}

TEST_F(EmbeddingManagerTest, PartialCaching) {
  manager_->SetCacheEnabled(true);

  // Cache some texts
  manager_->GetEmbedding("text1");
  manager_->GetEmbedding("text2");

  // Batch with partial cache
  std::vector<std::string> texts = {"text1", "text2", "text3"};
  auto embeddings = manager_->GetEmbeddingBatch(texts);

  EXPECT_EQ(embeddings.size(), 3);

  auto stats = manager_->GetCacheStats();
  EXPECT_EQ(stats.hits, 2);  // text1, text2
  EXPECT_EQ(stats.misses, 3); // text1, text2, text3
  EXPECT_EQ(stats.size, 3);
}

TEST_F(EmbeddingManagerTest, SwitchProvider) {
  manager_->SetProvider("mock");
  EXPECT_EQ(manager_->GetProviderName(), "mock");

  auto embedding = manager_->GetEmbedding("test");
  EXPECT_EQ(embedding.size(), 128);

  manager_->SetProvider("local");
  EXPECT_EQ(manager_->GetProviderName(), "local");
}

TEST_F(EmbeddingManagerTest, SwitchProviderClearsCache) {
  manager_->SetCacheEnabled(true);

  manager_->GetEmbedding("test");
  auto stats1 = manager_->GetCacheStats();
  EXPECT_EQ(stats1.size, 1);

  manager_->SetProvider("mock");
  auto stats2 = manager_->GetCacheStats();
  EXPECT_EQ(stats2.size, 0);
}

TEST_F(EmbeddingManagerTest, MaxCacheSize) {
  manager_->SetCacheEnabled(true);
  manager_->SetMaxCacheSize(2);

  manager_->GetEmbedding("text1");
  manager_->GetEmbedding("text2");
  manager_->GetEmbedding("text3");

  auto stats = manager_->GetCacheStats();
  EXPECT_LE(stats.size, 2);
}

TEST_F(EmbeddingManagerTest, InvalidProvider) {
  EXPECT_THROW(manager_->SetProvider("nonexistent"), std::invalid_argument);
}

TEST_F(EmbeddingManagerTest, BatchSizeMismatch) {
  std::vector<std::string> ids = {"doc1", "doc2"};
  std::vector<std::string> texts = {"text1"};

  EXPECT_FALSE(manager_->IndexTextBatch(ids, texts));
}

TEST_F(EmbeddingManagerTest, MetadataSizeMismatch) {
  std::vector<std::string> ids = {"doc1", "doc2"};
  std::vector<std::string> texts = {"text1", "text2"};
  std::vector<std::string> metadata = {"meta1"};

  EXPECT_FALSE(manager_->IndexTextBatch(ids, texts, metadata));
}

TEST_F(EmbeddingManagerTest, EmptyBatch) {
  std::vector<std::string> ids;
  std::vector<std::string> texts;

  EXPECT_TRUE(manager_->IndexTextBatch(ids, texts));
}
