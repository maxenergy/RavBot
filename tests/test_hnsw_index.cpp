// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/hnsw_index.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <random>

using namespace quantclaw;

class HNSWIndexTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);
  }

  std::shared_ptr<spdlog::logger> logger_;
};

// Test basic initialization
TEST_F(HNSWIndexTest, Initialize) {
  HNSWConfig config;
  config.dimension = 128;
  config.max_elements = 1000;

  HNSWIndex index(config, logger_);
  EXPECT_TRUE(index.Initialize());
  EXPECT_TRUE(index.IsInitialized());
  EXPECT_EQ(index.GetDimension(), 128);
  EXPECT_EQ(index.GetSize(), 0);
}

// Test adding a single vector
TEST_F(HNSWIndexTest, AddVector) {
  HNSWConfig config;
  config.dimension = 128;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  std::vector<float> vec(128, 1.0f);
  EXPECT_TRUE(index.AddVector("vec1", vec));
  EXPECT_EQ(index.GetSize(), 1);
}

// Test adding multiple vectors
TEST_F(HNSWIndexTest, AddMultipleVectors) {
  HNSWConfig config;
  config.dimension = 128;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  for (int i = 0; i < 10; ++i) {
    std::vector<float> vec(128, static_cast<float>(i));
    EXPECT_TRUE(index.AddVector("vec" + std::to_string(i), vec));
  }

  EXPECT_EQ(index.GetSize(), 10);
}

// Test batch adding vectors
TEST_F(HNSWIndexTest, AddVectorsBatch) {
  HNSWConfig config;
  config.dimension = 128;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  std::vector<std::pair<std::string, std::vector<float>>> batch;
  for (int i = 0; i < 100; ++i) {
    std::vector<float> vec(128, static_cast<float>(i));
    batch.emplace_back("vec" + std::to_string(i), vec);
  }

  EXPECT_TRUE(index.AddVectorsBatch(batch));
  EXPECT_EQ(index.GetSize(), 100);
}

// Test basic search
TEST_F(HNSWIndexTest, BasicSearch) {
  HNSWConfig config;
  config.dimension = 128;
  config.metric = "l2";

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  // Add some vectors
  std::vector<float> vec1(128, 1.0f);
  std::vector<float> vec2(128, 2.0f);
  std::vector<float> vec3(128, 3.0f);

  ASSERT_TRUE(index.AddVector("vec1", vec1));
  ASSERT_TRUE(index.AddVector("vec2", vec2));
  ASSERT_TRUE(index.AddVector("vec3", vec3));

  // Search for vec1
  auto results = index.Search(vec1, 3);
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0].first, "vec1");  // Closest should be itself
  EXPECT_NEAR(results[0].second, 0.0f, 0.01f);
}

// Test cosine similarity search
TEST_F(HNSWIndexTest, CosineSearch) {
  HNSWConfig config;
  config.dimension = 3;
  config.metric = "cosine";

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  // Add orthogonal vectors
  std::vector<float> vec1 = {1.0f, 0.0f, 0.0f};
  std::vector<float> vec2 = {0.0f, 1.0f, 0.0f};
  std::vector<float> vec3 = {0.0f, 0.0f, 1.0f};

  ASSERT_TRUE(index.AddVector("vec1", vec1));
  ASSERT_TRUE(index.AddVector("vec2", vec2));
  ASSERT_TRUE(index.AddVector("vec3", vec3));

  // Search for vec1
  auto results = index.Search(vec1, 3);
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0].first, "vec1");
  EXPECT_NEAR(results[0].second, 0.0f, 0.01f);  // Distance should be 0
}

// Test search with k larger than index size
TEST_F(HNSWIndexTest, SearchKLargerThanSize) {
  HNSWConfig config;
  config.dimension = 128;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  // Add 5 vectors
  for (int i = 0; i < 5; ++i) {
    std::vector<float> vec(128, static_cast<float>(i));
    ASSERT_TRUE(index.AddVector("vec" + std::to_string(i), vec));
  }

  // Search for 10 neighbors (more than available)
  std::vector<float> query(128, 2.5f);
  auto results = index.Search(query, 10);
  EXPECT_EQ(results.size(), 5);  // Should return only 5
}

// Test mark deleted
TEST_F(HNSWIndexTest, MarkDeleted) {
  HNSWConfig config;
  config.dimension = 128;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  std::vector<float> vec1(128, 1.0f);
  std::vector<float> vec2(128, 2.0f);

  ASSERT_TRUE(index.AddVector("vec1", vec1));
  ASSERT_TRUE(index.AddVector("vec2", vec2));

  EXPECT_TRUE(index.MarkDeleted("vec1"));

  // Search should not return deleted vector
  auto results = index.Search(vec1, 2);
  EXPECT_LE(results.size(), 1);
}

// Test save and load index
TEST_F(HNSWIndexTest, SaveAndLoad) {
  HNSWConfig config;
  config.dimension = 128;

  std::string index_path = "/tmp/test_hnsw_index.bin";

  // Create and save index
  {
    HNSWIndex index(config, logger_);
    ASSERT_TRUE(index.Initialize());

    for (int i = 0; i < 100; ++i) {
      std::vector<float> vec(128, static_cast<float>(i));
      ASSERT_TRUE(index.AddVector("vec" + std::to_string(i), vec));
    }

    EXPECT_TRUE(index.SaveIndex(index_path));
  }

  // Load index
  {
    HNSWIndex index(config, logger_);
    EXPECT_TRUE(index.LoadIndex(index_path));
    EXPECT_TRUE(index.IsInitialized());
    EXPECT_EQ(index.GetSize(), 100);

    // Test search on loaded index
    std::vector<float> query(128, 50.0f);
    auto results = index.Search(query, 5);
    EXPECT_EQ(results.size(), 5);
  }

  // Cleanup
  std::remove(index_path.c_str());
  std::remove((index_path + ".mapping").c_str());
}

// Test dimension mismatch
TEST_F(HNSWIndexTest, DimensionMismatch) {
  HNSWConfig config;
  config.dimension = 128;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  // Try to add vector with wrong dimension
  std::vector<float> vec(64, 1.0f);  // Wrong dimension
  EXPECT_FALSE(index.AddVector("vec1", vec));
}

// Test empty ID
TEST_F(HNSWIndexTest, EmptyId) {
  HNSWConfig config;
  config.dimension = 128;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  std::vector<float> vec(128, 1.0f);
  EXPECT_FALSE(index.AddVector("", vec));  // Empty ID
}

// Test search on empty index
TEST_F(HNSWIndexTest, SearchEmptyIndex) {
  HNSWConfig config;
  config.dimension = 128;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  std::vector<float> query(128, 1.0f);
  auto results = index.Search(query, 10);
  EXPECT_TRUE(results.empty());
}

// Test clear
TEST_F(HNSWIndexTest, Clear) {
  HNSWConfig config;
  config.dimension = 128;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  // Add vectors
  for (int i = 0; i < 10; ++i) {
    std::vector<float> vec(128, static_cast<float>(i));
    ASSERT_TRUE(index.AddVector("vec" + std::to_string(i), vec));
  }

  EXPECT_EQ(index.GetSize(), 10);

  // Clear
  index.Clear();
  EXPECT_FALSE(index.IsInitialized());
  EXPECT_EQ(index.GetSize(), 0);
}

// Performance test: measure search speed
TEST_F(HNSWIndexTest, PerformanceTest) {
  HNSWConfig config;
  config.dimension = 384;
  config.max_elements = 10000;
  config.M = 16;
  config.ef_construction = 200;
  config.ef_search = 50;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  // Add 10,000 random vectors
  std::mt19937 rng(42);
  std::normal_distribution<float> dist(0.0f, 1.0f);

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < 10000; ++i) {
    std::vector<float> vec(384);
    for (float& v : vec) {
      v = dist(rng);
    }
    ASSERT_TRUE(index.AddVector("vec" + std::to_string(i), vec));
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Added 10,000 vectors in " << duration.count() << " ms"
            << std::endl;
  std::cout << "Average: " << duration.count() / 10000.0 << " ms per vector"
            << std::endl;

  // Measure search speed
  std::vector<float> query(384);
  for (float& v : query) {
    v = dist(rng);
  }

  start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < 100; ++i) {
    auto results = index.Search(query, 10);
    EXPECT_EQ(results.size(), 10);
  }

  end = std::chrono::high_resolution_clock::now();
  auto search_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << "100 searches in " << search_duration.count() << " us" << std::endl;
  std::cout << "Average: " << search_duration.count() / 100.0 << " us per search"
            << std::endl;
}

// Test recall accuracy
TEST_F(HNSWIndexTest, RecallTest) {
  HNSWConfig config;
  config.dimension = 128;
  config.max_elements = 1000;
  config.M = 16;
  config.ef_construction = 200;
  config.ef_search = 50;

  HNSWIndex index(config, logger_);
  ASSERT_TRUE(index.Initialize());

  // Add 1000 random vectors
  std::mt19937 rng(42);
  std::normal_distribution<float> dist(0.0f, 1.0f);

  std::vector<std::vector<float>> vectors;
  for (int i = 0; i < 1000; ++i) {
    std::vector<float> vec(128);
    for (float& v : vec) {
      v = dist(rng);
    }
    vectors.push_back(vec);
    ASSERT_TRUE(index.AddVector("vec" + std::to_string(i), vec));
  }

  // Test recall: search for each vector and check if it's the top result
  int correct = 0;
  for (int i = 0; i < 1000; ++i) {
    auto results = index.Search(vectors[i], 1);
    ASSERT_EQ(results.size(), 1);
    if (results[0].first == "vec" + std::to_string(i)) {
      correct++;
    }
  }

  float recall = static_cast<float>(correct) / 1000.0f;
  std::cout << "Recall@1: " << recall * 100.0f << "%" << std::endl;
  EXPECT_GT(recall, 0.99f);  // Should have > 99% recall
}
