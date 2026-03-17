// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/core/embedding_manager.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ravbot {

EmbeddingManager::EmbeddingManager(
    std::shared_ptr<EmbeddingProviderRegistry> registry,
    std::shared_ptr<VectorDatabase> vector_db,
    std::shared_ptr<spdlog::logger> logger)
    : registry_(registry),
      vector_db_(vector_db),
      logger_(logger ? logger : spdlog::default_logger()),
      cache_enabled_(true),
      max_cache_size_(10000),
      cache_hits_(0),
      cache_misses_(0) {
  if (!registry_) {
    throw std::invalid_argument("EmbeddingProviderRegistry cannot be null");
  }
  if (!vector_db_) {
    throw std::invalid_argument("VectorDatabase cannot be null");
  }

  // Use default provider from registry
  auto default_provider = registry_->GetDefaultProvider();
  if (default_provider) {
    current_provider_ = default_provider->GetName();
  }
}

bool EmbeddingManager::IndexText(const std::string& id,
                                 const std::string& text,
                                 const std::string& metadata) {
  try {
    logger_->debug("[IndexText] START: id={}, text_len={}", id, text.size());

    logger_->debug("[IndexText] Calling GetEmbedding...");
    auto embedding = GetEmbedding(text);
    logger_->debug("[IndexText] GetEmbedding returned, embedding_size={}", embedding.size());

    nlohmann::json meta_json;
    if (!metadata.empty()) {
      try {
        meta_json = nlohmann::json::parse(metadata);
      } catch (...) {
        meta_json = {{"raw", metadata}};
      }
    }

    logger_->debug("[IndexText] Calling vector_db_->IndexVector...");
    bool result = vector_db_->IndexVector(id, embedding, text, meta_json);
    logger_->debug("[IndexText] IndexVector returned: {}", result);

    return result;
  } catch (const std::exception& e) {
    logger_->error("Failed to index text '{}': {}", id, e.what());
    return false;
  }
}

bool EmbeddingManager::IndexTextBatch(
    const std::vector<std::string>& ids,
    const std::vector<std::string>& texts,
    const std::vector<std::string>& metadata) {
  if (ids.size() != texts.size()) {
    logger_->error("IDs and texts size mismatch: {} vs {}", ids.size(), texts.size());
    return false;
  }

  if (!metadata.empty() && metadata.size() != ids.size()) {
    logger_->error("Metadata size mismatch: {} vs {}", metadata.size(), ids.size());
    return false;
  }

  try {
    auto embeddings = GetEmbeddingBatch(texts);

    bool all_success = true;
    for (size_t i = 0; i < ids.size(); ++i) {
      nlohmann::json meta_json;
      if (!metadata.empty() && !metadata[i].empty()) {
        try {
          meta_json = nlohmann::json::parse(metadata[i]);
        } catch (...) {
          meta_json = {{"raw", metadata[i]}};
        }
      }

      if (!vector_db_->IndexVector(ids[i], embeddings[i], texts[i], meta_json)) {
        logger_->warn("Failed to index text '{}'", ids[i]);
        all_success = false;
      }
    }

    return all_success;
  } catch (const std::exception& e) {
    logger_->error("Failed to index text batch: {}", e.what());
    return false;
  }
}

std::vector<VectorSearchResult> EmbeddingManager::SearchText(
    const std::string& query,
    int top_k,
    float threshold) {
  try {
    auto query_embedding = GetEmbedding(query);
    return vector_db_->SearchVectors(query_embedding, top_k, threshold);
  } catch (const std::exception& e) {
    logger_->error("Failed to search text: {}", e.what());
    return {};
  }
}

std::vector<float> EmbeddingManager::GetEmbedding(const std::string& text) {
  // Check cache first
  if (cache_enabled_) {
    std::vector<float> cached_embedding;
    if (GetFromCache(text, cached_embedding)) {
      return cached_embedding;
    }
  }

  // Generate embedding
  auto provider = GetCurrentProvider();
  if (!provider) {
    throw std::runtime_error("No embedding provider available");
  }

  auto embedding = provider->Embed(text);

  // Add to cache
  if (cache_enabled_) {
    AddToCache(text, embedding);
  }

  return embedding;
}

std::vector<std::vector<float>> EmbeddingManager::GetEmbeddingBatch(
    const std::vector<std::string>& texts) {
  if (texts.empty()) {
    return {};
  }

  // Check cache for each text
  std::vector<std::vector<float>> results;
  std::vector<std::string> uncached_texts;
  std::vector<size_t> uncached_indices;

  results.resize(texts.size());

  if (cache_enabled_) {
    for (size_t i = 0; i < texts.size(); ++i) {
      std::vector<float> cached_embedding;
      if (GetFromCache(texts[i], cached_embedding)) {
        results[i] = std::move(cached_embedding);
      } else {
        uncached_texts.push_back(texts[i]);
        uncached_indices.push_back(i);
      }
    }
  } else {
    uncached_texts = texts;
    for (size_t i = 0; i < texts.size(); ++i) {
      uncached_indices.push_back(i);
    }
  }

  // Generate embeddings for uncached texts
  if (!uncached_texts.empty()) {
    auto provider = GetCurrentProvider();
    if (!provider) {
      throw std::runtime_error("No embedding provider available");
    }

    auto uncached_embeddings = provider->EmbedBatch(uncached_texts);

    // Fill results and cache
    for (size_t i = 0; i < uncached_texts.size(); ++i) {
      size_t result_index = uncached_indices[i];
      results[result_index] = uncached_embeddings[i];

      if (cache_enabled_) {
        AddToCache(uncached_texts[i], uncached_embeddings[i]);
      }
    }
  }

  return results;
}

void EmbeddingManager::SetProvider(const std::string& provider_name) {
  auto provider = registry_->GetProvider(provider_name);
  if (!provider) {
    throw std::invalid_argument("Provider not found: " + provider_name);
  }

  current_provider_ = provider_name;
  logger_->info("Switched to embedding provider: {}", provider_name);

  // Clear cache when switching providers
  ClearCache();
}

std::string EmbeddingManager::GetProviderName() const {
  return current_provider_;
}

void EmbeddingManager::SetCacheEnabled(bool enabled) {
  cache_enabled_ = enabled;
  if (!enabled) {
    ClearCache();
  }
  logger_->info("Embedding cache {}", enabled ? "enabled" : "disabled");
}

void EmbeddingManager::ClearCache() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  cache_.clear();
  cache_hits_ = 0;
  cache_misses_ = 0;
  logger_->info("Embedding cache cleared");
}

EmbeddingManager::CacheStats EmbeddingManager::GetCacheStats() const {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  return {cache_hits_, cache_misses_, cache_.size()};
}

void EmbeddingManager::SetMaxCacheSize(size_t max_size) {
  max_cache_size_ = max_size;
  logger_->info("Max cache size set to: {}", max_size);

  // Trim cache if needed
  std::lock_guard<std::mutex> lock(cache_mutex_);
  if (cache_.size() > max_cache_size_) {
    // Simple eviction: clear entire cache
    // TODO: Implement proper LRU eviction
    cache_.clear();
    logger_->warn("Cache cleared due to size limit");
  }
}

std::shared_ptr<EmbeddingProvider> EmbeddingManager::GetCurrentProvider() const {
  if (current_provider_.empty()) {
    return registry_->GetDefaultProvider();
  }
  return registry_->GetProvider(current_provider_);
}

void EmbeddingManager::AddToCache(const std::string& text,
                                  const std::vector<float>& embedding) {
  std::lock_guard<std::mutex> lock(cache_mutex_);

  // Check cache size limit
  if (cache_.size() >= max_cache_size_) {
    // Simple eviction: remove oldest entry
    // TODO: Implement proper LRU eviction
    auto it = cache_.begin();
    cache_.erase(it);
  }

  cache_[text] = embedding;
}

bool EmbeddingManager::GetFromCache(const std::string& text,
                                    std::vector<float>& embedding) const {
  std::lock_guard<std::mutex> lock(cache_mutex_);

  auto it = cache_.find(text);
  if (it != cache_.end()) {
    embedding = it->second;
    ++cache_hits_;
    return true;
  }

  ++cache_misses_;
  return false;
}

}  // namespace ravbot
