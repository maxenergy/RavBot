// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ravbot/core/embedding_provider.hpp"
#include "ravbot/core/vector_database.hpp"

#include <spdlog/spdlog.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace ravbot {

// EmbeddingManager integrates embedding providers with vector database
// Provides high-level API for text indexing and search
class EmbeddingManager {
 public:
  EmbeddingManager(std::shared_ptr<EmbeddingProviderRegistry> registry,
                   std::shared_ptr<VectorDatabase> vector_db,
                   std::shared_ptr<spdlog::logger> logger = nullptr);
  ~EmbeddingManager() = default;

  // Index text with automatic embedding
  bool IndexText(const std::string& id,
                 const std::string& text,
                 const std::string& metadata = "");

  // Index multiple texts in batch
  bool IndexTextBatch(const std::vector<std::string>& ids,
                      const std::vector<std::string>& texts,
                      const std::vector<std::string>& metadata = {});

  // Search by text query
  std::vector<VectorSearchResult> SearchText(
      const std::string& query,
      int top_k = 10,
      float threshold = 0.0f);

  // Get embedding for text (with caching)
  std::vector<float> GetEmbedding(const std::string& text);

  // Get embeddings for multiple texts (with caching)
  std::vector<std::vector<float>> GetEmbeddingBatch(
      const std::vector<std::string>& texts);

  // Set the active embedding provider
  void SetProvider(const std::string& provider_name);

  // Get current provider name
  std::string GetProviderName() const;

  // Enable/disable caching
  void SetCacheEnabled(bool enabled);

  // Clear embedding cache
  void ClearCache();

  // Get cache statistics
  struct CacheStats {
    size_t hits;
    size_t misses;
    size_t size;
  };
  CacheStats GetCacheStats() const;

  // Set maximum cache size (number of entries)
  void SetMaxCacheSize(size_t max_size);

 private:
  std::shared_ptr<EmbeddingProviderRegistry> registry_;
  std::shared_ptr<VectorDatabase> vector_db_;
  std::shared_ptr<spdlog::logger> logger_;

  std::string current_provider_;
  bool cache_enabled_;
  size_t max_cache_size_;

  // Cache: text -> embedding
  std::unordered_map<std::string, std::vector<float>> cache_;
  mutable std::mutex cache_mutex_;

  // Cache statistics
  mutable size_t cache_hits_;
  mutable size_t cache_misses_;

  // Get current provider
  std::shared_ptr<EmbeddingProvider> GetCurrentProvider() const;

  // Add to cache (with LRU eviction)
  void AddToCache(const std::string& text, const std::vector<float>& embedding);

  // Get from cache
  bool GetFromCache(const std::string& text, std::vector<float>& embedding) const;
};

}  // namespace ravbot
