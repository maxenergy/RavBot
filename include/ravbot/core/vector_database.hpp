// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <memory>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace ravbot {

// Forward declaration
class HNSWIndex;
struct HNSWConfig;

struct EmbeddingVector {
    std::string id;
    std::vector<float> vector;
    nlohmann::json metadata;
    std::string text;
    int64_t timestamp;
};

struct VectorSearchResult {
    std::string id;
    float distance;
    nlohmann::json metadata;
    std::string text;
};

// Configuration for VectorDatabase
struct VectorDatabaseConfig {
  bool use_hnsw = true;              // Use HNSW index for fast search
  int hnsw_M = 16;                   // HNSW M parameter
  int hnsw_ef_construction = 200;    // HNSW ef_construction parameter
  int hnsw_ef_search = 50;           // HNSW ef_search parameter
  int max_elements = 100000;         // Maximum number of vectors
  std::string metric = "cosine";     // Distance metric: "cosine", "l2", "ip"
};

class VectorDatabase {
 public:
  VectorDatabase(const std::string& db_path,
                 std::shared_ptr<spdlog::logger> logger);
  VectorDatabase(const std::string& db_path,
                 std::shared_ptr<spdlog::logger> logger,
                 const VectorDatabaseConfig& config);
  ~VectorDatabase();

  // Initialize database and create tables
  bool Initialize();

  // Vector operations
  bool IndexVector(const std::string& id, const std::vector<float>& vector,
                   const std::string& text,
                   const nlohmann::json& metadata = {});

  std::vector<VectorSearchResult> SearchVectors(
      const std::vector<float>& query_vector, int limit = 10,
      float threshold = 0.7f);

  bool DeleteVector(const std::string& id);
  bool DeleteAll();

  // Get statistics
  int64_t GetVectorCount();
  int GetDimension();

  // HNSW index management
  bool RebuildIndex();                    // Rebuild HNSW index from SQLite
  bool SaveIndex();                       // Save HNSW index to disk
  bool LoadIndex();                       // Load HNSW index from disk
  nlohmann::json GetIndexStats();         // Get index statistics

  // Check if using HNSW
  bool IsUsingHNSW() const { return use_hnsw_; }

  // FTS5 full-text search (BM25 ranked).
  // Returns results sorted by BM25 relevance (best first).
  std::vector<VectorSearchResult> SearchFTS(
      const std::string& query, int limit = 10);

  // Hybrid search: combines HNSW vector search and FTS5 BM25 using
  // Reciprocal Rank Fusion (RRF, k=60). Runs both searches concurrently
  // and merges results. Returns deduplicated, RRF-ranked results.
  std::vector<VectorSearchResult> HybridSearch(
      const std::vector<float>& query_vector,
      const std::string& query_text,
      int limit = 10,
      float threshold = 0.0f);

 private:
  bool CreateTables();
  bool LoadDimension();
  bool LoadExtension();
  bool InitializeHNSW();
  std::string GetIndexPath() const;

  std::string db_path_;
  std::shared_ptr<spdlog::logger> logger_;
  sqlite3* db_ = nullptr;
  int dimension_ = 0;
  std::mutex mutex_;

  // HNSW integration
  VectorDatabaseConfig config_;
  std::unique_ptr<HNSWIndex> hnsw_index_;
  bool use_hnsw_ = true;
  bool hnsw_initialized_ = false;
};

}  // namespace ravbot
