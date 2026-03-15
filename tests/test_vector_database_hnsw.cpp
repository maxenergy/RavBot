// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/vector_database.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <random>

using namespace quantclaw;

class VectorDatabaseHNSWTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    db_path_ = "/tmp/test_vector_db_hnsw.db";
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_ + ".hnsw");
    std::filesystem::remove(db_path_ + ".hnsw.mapping");
  }

  void TearDown() override {
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_ + ".hnsw");
    std::filesystem::remove(db_path_ + ".hnsw.mapping");
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::string db_path_;
};

// Test basic initialization with HNSW
TEST_F(VectorDatabaseHNSWTest, InitializeWithHNSW) {
  VectorDatabaseConfig config;
  config.use_hnsw = true;

  VectorDatabase db(db_path_, logger_, config);
  EXPECT_TRUE(db.Initialize());
  EXPECT_TRUE(db.IsUsingHNSW());
}

// Test initialization without HNSW
TEST_F(VectorDatabaseHNSWTest, InitializeWithoutHNSW) {
  VectorDatabaseConfig config;
  config.use_hnsw = false;

  VectorDatabase db(db_path_, logger_, config);
  EXPECT_TRUE(db.Initialize());
  EXPECT_FALSE(db.IsUsingHNSW());
}

// Test indexing and searching with HNSW
TEST_F(VectorDatabaseHNSWTest, IndexAndSearchWithHNSW) {
  VectorDatabaseConfig config;
  config.use_hnsw = true;

  VectorDatabase db(db_path_, logger_, config);
  ASSERT_TRUE(db.Initialize());

  // Add some vectors with diverse patterns (avoid constant values)
  std::vector<float> vec1(128);
  std::vector<float> vec2(128);
  std::vector<float> vec3(128);

  for (int i = 0; i < 128; ++i) {
    vec1[i] = static_cast<float>(i) / 128.0f;           // Linear pattern
    vec2[i] = static_cast<float>(128 - i) / 128.0f;     // Reverse linear
    vec3[i] = (i % 2 == 0) ? 1.0f : 0.0f;               // Alternating pattern
  }

  ASSERT_TRUE(db.IndexVector("doc1", vec1, "text1"));
  ASSERT_TRUE(db.IndexVector("doc2", vec2, "text2"));
  ASSERT_TRUE(db.IndexVector("doc3", vec3, "text3"));

  EXPECT_EQ(db.GetVectorCount(), 3);

  // Search
  auto results = db.SearchVectors(vec1, 3, 0.0f);
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0].id, "doc1");  // Closest should be itself
}

// Test HNSW performance vs brute-force
TEST_F(VectorDatabaseHNSWTest, PerformanceComparison) {
  const int num_vectors = 1000;
  const int dimension = 128;

  // Generate random vectors
  std::mt19937 rng(42);
  std::normal_distribution<float> dist(0.0f, 1.0f);

  std::vector<std::pair<std::string, std::vector<float>>> vectors;
  for (int i = 0; i < num_vectors; ++i) {
    std::vector<float> vec(dimension);
    for (float& v : vec) {
      v = dist(rng);
    }
    vectors.emplace_back("doc" + std::to_string(i), vec);
  }

  // Test with HNSW
  {
    VectorDatabaseConfig config;
    config.use_hnsw = true;

    VectorDatabase db("/tmp/test_hnsw_perf.db", logger_, config);
    ASSERT_TRUE(db.Initialize());

    // Index all vectors
    auto start = std::chrono::high_resolution_clock::now();
    for (const auto& [id, vec] : vectors) {
      ASSERT_TRUE(db.IndexVector(id, vec, "text"));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto index_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Search
    std::vector<float> query(dimension);
    for (float& v : query) {
      v = dist(rng);
    }

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; ++i) {
      auto results = db.SearchVectors(query, 10, 0.0f);
      EXPECT_EQ(results.size(), 10);
    }
    end = std::chrono::high_resolution_clock::now();
    auto search_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "HNSW - Index time: " << index_time.count() << " ms" << std::endl;
    std::cout << "HNSW - 10 searches: " << search_time.count() << " us" << std::endl;
    std::cout << "HNSW - Avg search: " << search_time.count() / 10.0 << " us" << std::endl;

    std::filesystem::remove("/tmp/test_hnsw_perf.db");
    std::filesystem::remove("/tmp/test_hnsw_perf.db.hnsw");
    std::filesystem::remove("/tmp/test_hnsw_perf.db.hnsw.mapping");
  }

  // Test without HNSW (brute-force)
  {
    VectorDatabaseConfig config;
    config.use_hnsw = false;

    VectorDatabase db("/tmp/test_bruteforce_perf.db", logger_, config);
    ASSERT_TRUE(db.Initialize());

    // Index all vectors
    auto start = std::chrono::high_resolution_clock::now();
    for (const auto& [id, vec] : vectors) {
      ASSERT_TRUE(db.IndexVector(id, vec, "text"));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto index_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Search
    std::vector<float> query(dimension);
    for (float& v : query) {
      v = dist(rng);
    }

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; ++i) {
      auto results = db.SearchVectors(query, 10, 0.0f);
      EXPECT_EQ(results.size(), 10);
    }
    end = std::chrono::high_resolution_clock::now();
    auto search_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Brute-force - Index time: " << index_time.count() << " ms" << std::endl;
    std::cout << "Brute-force - 10 searches: " << search_time.count() << " us" << std::endl;
    std::cout << "Brute-force - Avg search: " << search_time.count() / 10.0 << " us" << std::endl;

    std::filesystem::remove("/tmp/test_bruteforce_perf.db");
  }
}

// Test rebuild index
TEST_F(VectorDatabaseHNSWTest, RebuildIndex) {
  VectorDatabaseConfig config;
  config.use_hnsw = true;

  VectorDatabase db(db_path_, logger_, config);
  ASSERT_TRUE(db.Initialize());

  // Add vectors with diverse patterns
  for (int i = 0; i < 100; ++i) {
    std::vector<float> vec(128);
    for (int j = 0; j < 128; ++j) {
      vec[j] = static_cast<float>(i * 128 + j) / 12800.0f;
    }
    ASSERT_TRUE(db.IndexVector("doc" + std::to_string(i), vec, "text"));
  }

  // Rebuild index
  EXPECT_TRUE(db.RebuildIndex());

  // Search with a query vector
  std::vector<float> query(128);
  for (int i = 0; i < 128; ++i) {
    query[i] = static_cast<float>(50 * 128 + i) / 12800.0f;
  }
  auto results = db.SearchVectors(query, 5, 0.0f);
  EXPECT_EQ(results.size(), 5);
}

// Test save and load index
TEST_F(VectorDatabaseHNSWTest, SaveAndLoadIndex) {
  VectorDatabaseConfig config;
  config.use_hnsw = true;

  // Create and save
  {
    VectorDatabase db(db_path_, logger_, config);
    ASSERT_TRUE(db.Initialize());

    for (int i = 0; i < 100; ++i) {
      std::vector<float> vec(128);
      for (int j = 0; j < 128; ++j) {
        vec[j] = static_cast<float>(i * 128 + j) / 12800.0f;
      }
      ASSERT_TRUE(db.IndexVector("doc" + std::to_string(i), vec, "text"));
    }

    EXPECT_TRUE(db.SaveIndex());
  }

  // Load and search
  {
    VectorDatabase db(db_path_, logger_, config);
    ASSERT_TRUE(db.Initialize());

    std::vector<float> query(128);
    for (int i = 0; i < 128; ++i) {
      query[i] = static_cast<float>(50 * 128 + i) / 12800.0f;
    }
    auto results = db.SearchVectors(query, 5, 0.0f);
    EXPECT_EQ(results.size(), 5);
  }
}

// Test delete vector
TEST_F(VectorDatabaseHNSWTest, DeleteVector) {
  VectorDatabaseConfig config;
  config.use_hnsw = true;

  VectorDatabase db(db_path_, logger_, config);
  ASSERT_TRUE(db.Initialize());

  // Create diverse vectors
  std::vector<float> vec1(128);
  std::vector<float> vec2(128);
  for (int i = 0; i < 128; ++i) {
    vec1[i] = static_cast<float>(i) / 128.0f;
    vec2[i] = static_cast<float>(128 - i) / 128.0f;
  }

  ASSERT_TRUE(db.IndexVector("doc1", vec1, "text1"));
  ASSERT_TRUE(db.IndexVector("doc2", vec2, "text2"));

  EXPECT_EQ(db.GetVectorCount(), 2);

  EXPECT_TRUE(db.DeleteVector("doc1"));
  EXPECT_EQ(db.GetVectorCount(), 1);

  // Search should not return deleted vector
  auto results = db.SearchVectors(vec1, 2, 0.0f);
  EXPECT_LE(results.size(), 1);
}

// Test delete all
TEST_F(VectorDatabaseHNSWTest, DeleteAll) {
  VectorDatabaseConfig config;
  config.use_hnsw = true;

  VectorDatabase db(db_path_, logger_, config);
  ASSERT_TRUE(db.Initialize());

  for (int i = 0; i < 10; ++i) {
    std::vector<float> vec(128);
    for (int j = 0; j < 128; ++j) {
      vec[j] = static_cast<float>(i * 128 + j) / 1280.0f;
    }
    ASSERT_TRUE(db.IndexVector("doc" + std::to_string(i), vec, "text"));
  }

  EXPECT_EQ(db.GetVectorCount(), 10);

  EXPECT_TRUE(db.DeleteAll());
  EXPECT_EQ(db.GetVectorCount(), 0);
}

// Test get index stats
TEST_F(VectorDatabaseHNSWTest, GetIndexStats) {
  VectorDatabaseConfig config;
  config.use_hnsw = true;
  config.hnsw_M = 16;
  config.hnsw_ef_construction = 200;
  config.hnsw_ef_search = 50;

  VectorDatabase db(db_path_, logger_, config);
  ASSERT_TRUE(db.Initialize());

  for (int i = 0; i < 10; ++i) {
    std::vector<float> vec(128);
    for (int j = 0; j < 128; ++j) {
      vec[j] = static_cast<float>(i * 128 + j) / 1280.0f;
    }
    ASSERT_TRUE(db.IndexVector("doc" + std::to_string(i), vec, "text"));
  }

  auto stats = db.GetIndexStats();
  EXPECT_TRUE(stats["use_hnsw"].get<bool>());
  EXPECT_TRUE(stats["hnsw_initialized"].get<bool>());
  EXPECT_EQ(stats["dimension"].get<int>(), 128);
  EXPECT_EQ(stats["vector_count"].get<int64_t>(), 10);
  EXPECT_EQ(stats["hnsw_size"].get<int>(), 10);
  EXPECT_EQ(stats["hnsw_M"].get<int>(), 16);
  EXPECT_EQ(stats["hnsw_ef_construction"].get<int>(), 200);
  EXPECT_EQ(stats["hnsw_ef_search"].get<int>(), 50);
}
