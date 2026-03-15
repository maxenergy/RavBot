// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

// Forward declare hnswlib types to avoid including the header here
namespace hnswlib {
template <typename dist_t>
class SpaceInterface;
template <typename dist_t>
class HierarchicalNSW;
}  // namespace hnswlib

namespace quantclaw {

// HNSW index configuration
struct HNSWConfig {
  int dimension = 384;           // Vector dimension
  int max_elements = 100000;     // Maximum number of vectors
  int M = 16;                    // Number of connections per node (4-48)
  int ef_construction = 200;     // Search depth during construction (100-500)
  int ef_search = 50;            // Search depth during query (10-500)
  std::string metric = "cosine"; // "cosine", "l2", "ip" (inner product)
  bool allow_replace = false;    // Allow replacing existing vectors
};

// HNSW index wrapper for fast approximate nearest neighbor search
class HNSWIndex {
 public:
  HNSWIndex(const HNSWConfig& config, std::shared_ptr<spdlog::logger> logger);
  ~HNSWIndex();

  // Initialize the index
  bool Initialize();

  // Add a vector to the index
  bool AddVector(const std::string& id, const std::vector<float>& vector);

  // Add multiple vectors in batch (more efficient)
  bool AddVectorsBatch(
      const std::vector<std::pair<std::string, std::vector<float>>>& vectors);

  // Search for k nearest neighbors
  // Returns vector of (id, distance) pairs sorted by distance
  std::vector<std::pair<std::string, float>> Search(
      const std::vector<float>& query, int k = 10);

  // Mark a vector as deleted (soft delete)
  bool MarkDeleted(const std::string& id);

  // Save index to file
  bool SaveIndex(const std::string& path);

  // Load index from file
  bool LoadIndex(const std::string& path);

  // Clear all data
  void Clear();

  // Get statistics
  int GetSize() const;
  int GetDimension() const { return config_.dimension; }
  int GetMaxElements() const { return config_.max_elements; }

  // Check if index is initialized
  bool IsInitialized() const { return initialized_; }

 private:
  // Internal label type (hnswlib uses size_t)
  using label_t = size_t;

  // Convert string ID to internal label
  label_t GetLabel(const std::string& id);

  // Get string ID from internal label
  std::string GetId(label_t label) const;

  HNSWConfig config_;
  std::shared_ptr<spdlog::logger> logger_;

  // hnswlib objects (using void* to avoid exposing hnswlib types)
  void* space_ = nullptr;  // SpaceInterface<float>*
  void* index_ = nullptr;  // HierarchicalNSW<float>*

  // ID mapping
  std::unordered_map<std::string, label_t> id_to_label_;
  std::unordered_map<label_t, std::string> label_to_id_;
  label_t next_label_ = 0;

  // Thread safety
  mutable std::mutex mutex_;

  bool initialized_ = false;
};

}  // namespace quantclaw
