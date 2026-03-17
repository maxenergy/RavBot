// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/core/hnsw_index.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

// Include hnswlib headers
#include "hnswlib/hnswlib.h"

namespace ravbot {

HNSWIndex::HNSWIndex(const HNSWConfig& config,
                     std::shared_ptr<spdlog::logger> logger)
    : config_(config), logger_(logger) {}

HNSWIndex::~HNSWIndex() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (index_) {
    delete static_cast<hnswlib::HierarchicalNSW<float>*>(index_);
    index_ = nullptr;
  }

  if (space_) {
    delete static_cast<hnswlib::SpaceInterface<float>*>(space_);
    space_ = nullptr;
  }
}

bool HNSWIndex::Initialize() {
  logger_->debug("[HNSWIndex::Initialize] START");
  std::lock_guard<std::mutex> lock(mutex_);
  logger_->debug("[HNSWIndex::Initialize] Mutex acquired");

  if (initialized_) {
    logger_->warn("HNSW index already initialized");
    return true;
  }

  try {
    logger_->debug("[HNSWIndex::Initialize] Creating space for metric: {}", config_.metric);
    // Create space based on metric
    if (config_.metric == "cosine") {
      space_ = new hnswlib::InnerProductSpace(config_.dimension);
    } else if (config_.metric == "l2") {
      space_ = new hnswlib::L2Space(config_.dimension);
    } else if (config_.metric == "ip") {
      space_ = new hnswlib::InnerProductSpace(config_.dimension);
    } else {
      logger_->error("Unknown metric: {}", config_.metric);
      return false;
    }
    logger_->debug("[HNSWIndex::Initialize] Space created");

    logger_->debug("[HNSWIndex::Initialize] Creating HierarchicalNSW with max_elements={}, M={}, ef_construction={}",
                   config_.max_elements, config_.M, config_.ef_construction);
    // Create HNSW index
    auto* hnsw = new hnswlib::HierarchicalNSW<float>(
        static_cast<hnswlib::SpaceInterface<float>*>(space_),
        config_.max_elements, config_.M, config_.ef_construction);
    logger_->debug("[HNSWIndex::Initialize] HierarchicalNSW created");

    logger_->debug("[HNSWIndex::Initialize] Setting ef_search={}", config_.ef_search);
    hnsw->setEf(config_.ef_search);
    index_ = hnsw;

    initialized_ = true;
    logger_->info("HNSW index initialized: dim={}, max_elements={}, M={}, "
                  "ef_construction={}, ef_search={}, metric={}",
                  config_.dimension, config_.max_elements, config_.M,
                  config_.ef_construction, config_.ef_search, config_.metric);
    return true;

  } catch (const std::exception& e) {
    logger_->error("Failed to initialize HNSW index: {}", e.what());
    if (space_) {
      delete static_cast<hnswlib::SpaceInterface<float>*>(space_);
      space_ = nullptr;
    }
    return false;
  }
}

HNSWIndex::label_t HNSWIndex::GetLabel(const std::string& id) {
  auto it = id_to_label_.find(id);
  if (it != id_to_label_.end()) {
    return it->second;
  }

  label_t label = next_label_++;
  id_to_label_[id] = label;
  label_to_id_[label] = id;
  return label;
}

std::string HNSWIndex::GetId(label_t label) const {
  auto it = label_to_id_.find(label);
  if (it != label_to_id_.end()) {
    return it->second;
  }
  return "";
}

bool HNSWIndex::AddVector(const std::string& id,
                          const std::vector<float>& vector) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    logger_->error("HNSW index not initialized");
    return false;
  }

  if (id.empty()) {
    logger_->error("Cannot add vector with empty id");
    return false;
  }

  if (static_cast<int>(vector.size()) != config_.dimension) {
    logger_->error("Vector dimension mismatch: expected {}, got {}",
                   config_.dimension, vector.size());
    return false;
  }

  try {
    auto* hnsw = static_cast<hnswlib::HierarchicalNSW<float>*>(index_);
    label_t label = GetLabel(id);

    // Normalize vector for cosine similarity
    std::vector<float> normalized_vector = vector;
    if (config_.metric == "cosine") {
      float norm = 0.0f;
      for (float v : vector) {
        norm += v * v;
      }
      if (norm > 0.0f) {
        norm = std::sqrt(norm);
        for (float& v : normalized_vector) {
          v /= norm;
        }
      }
    }

    hnsw->addPoint(normalized_vector.data(), label, config_.allow_replace);
    return true;

  } catch (const std::exception& e) {
    logger_->error("Failed to add vector: {}", e.what());
    return false;
  }
}

bool HNSWIndex::AddVectorsBatch(
    const std::vector<std::pair<std::string, std::vector<float>>>& vectors) {
  if (vectors.empty()) {
    return true;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    logger_->error("HNSW index not initialized");
    return false;
  }

  try {
    auto* hnsw = static_cast<hnswlib::HierarchicalNSW<float>*>(index_);
    int success_count = 0;

    for (const auto& [id, vector] : vectors) {
      if (id.empty() || static_cast<int>(vector.size()) != config_.dimension) {
        continue;
      }

      label_t label = GetLabel(id);

      // Normalize vector for cosine similarity
      std::vector<float> normalized_vector = vector;
      if (config_.metric == "cosine") {
        float norm = 0.0f;
        for (float v : vector) {
          norm += v * v;
        }
        if (norm > 0.0f) {
          norm = std::sqrt(norm);
          for (float& v : normalized_vector) {
            v /= norm;
          }
        }
      }

      try {
        hnsw->addPoint(normalized_vector.data(), label, config_.allow_replace);
        success_count++;
      } catch (const std::exception& e) {
        logger_->warn("Failed to add vector {}: {}", id, e.what());
      }
    }

    logger_->info("Added {}/{} vectors to HNSW index", success_count,
                  vectors.size());
    return success_count > 0;

  } catch (const std::exception& e) {
    logger_->error("Failed to add vectors batch: {}", e.what());
    return false;
  }
}

std::vector<std::pair<std::string, float>> HNSWIndex::Search(
    const std::vector<float>& query, int k) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::pair<std::string, float>> results;

  if (!initialized_) {
    logger_->error("HNSW index not initialized");
    return results;
  }

  if (static_cast<int>(query.size()) != config_.dimension) {
    logger_->error("Query vector dimension mismatch: expected {}, got {}",
                   config_.dimension, query.size());
    return results;
  }

  if (k <= 0) {
    return results;
  }

  try {
    auto* hnsw = static_cast<hnswlib::HierarchicalNSW<float>*>(index_);

    if (hnsw->cur_element_count == 0) {
      return results;
    }

    // Normalize query vector for cosine similarity
    std::vector<float> normalized_query = query;
    if (config_.metric == "cosine") {
      float norm = 0.0f;
      for (float v : query) {
        norm += v * v;
      }
      if (norm > 0.0f) {
        norm = std::sqrt(norm);
        for (float& v : normalized_query) {
          v /= norm;
        }
      }
    }

    // Limit k to actual number of elements
    int actual_k = std::min(k, static_cast<int>(hnsw->cur_element_count));

    // Search
    auto result = hnsw->searchKnn(normalized_query.data(), actual_k);

    // Convert results
    results.reserve(result.size());
    while (!result.empty()) {
      auto [dist, label] = result.top();
      result.pop();

      std::string id = GetId(label);
      if (!id.empty()) {
        // HNSWlib already returns distance (not similarity)
        // - For InnerProductSpace: distance = 1 - inner_product
        // - For L2Space: distance = L2 distance
        // No conversion needed
        results.emplace_back(id, dist);
      }
    }

    // Reverse to get ascending order
    std::reverse(results.begin(), results.end());

    return results;

  } catch (const std::exception& e) {
    logger_->error("Failed to search HNSW index: {}", e.what());
    return results;
  }
}

bool HNSWIndex::MarkDeleted(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    logger_->error("HNSW index not initialized");
    return false;
  }

  auto it = id_to_label_.find(id);
  if (it == id_to_label_.end()) {
    logger_->warn("Vector not found: {}", id);
    return false;
  }

  try {
    auto* hnsw = static_cast<hnswlib::HierarchicalNSW<float>*>(index_);
    hnsw->markDelete(it->second);
    return true;

  } catch (const std::exception& e) {
    logger_->error("Failed to mark vector as deleted: {}", e.what());
    return false;
  }
}

bool HNSWIndex::SaveIndex(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    logger_->error("HNSW index not initialized");
    return false;
  }

  try {
    auto* hnsw = static_cast<hnswlib::HierarchicalNSW<float>*>(index_);
    hnsw->saveIndex(path);

    // Save ID mappings
    std::string mapping_path = path + ".mapping";
    std::ofstream ofs(mapping_path, std::ios::binary);
    if (!ofs) {
      logger_->error("Failed to open mapping file: {}", mapping_path);
      return false;
    }

    // Write number of mappings
    size_t count = id_to_label_.size();
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Write each mapping
    for (const auto& [id, label] : id_to_label_) {
      size_t id_len = id.size();
      ofs.write(reinterpret_cast<const char*>(&id_len), sizeof(id_len));
      ofs.write(id.data(), id_len);
      ofs.write(reinterpret_cast<const char*>(&label), sizeof(label));
    }

    // Write next_label_
    ofs.write(reinterpret_cast<const char*>(&next_label_), sizeof(next_label_));

    logger_->info("HNSW index saved to: {}", path);
    return true;

  } catch (const std::exception& e) {
    logger_->error("Failed to save HNSW index: {}", e.what());
    return false;
  }
}

bool HNSWIndex::LoadIndex(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    logger_->warn("HNSW index already initialized, clearing first");
    if (index_) {
      delete static_cast<hnswlib::HierarchicalNSW<float>*>(index_);
      index_ = nullptr;
    }
    if (space_) {
      delete static_cast<hnswlib::SpaceInterface<float>*>(space_);
      space_ = nullptr;
    }
    id_to_label_.clear();
    label_to_id_.clear();
    next_label_ = 0;
    initialized_ = false;
  }

  try {
    // Create space
    if (config_.metric == "cosine" || config_.metric == "ip") {
      space_ = new hnswlib::InnerProductSpace(config_.dimension);
    } else if (config_.metric == "l2") {
      space_ = new hnswlib::L2Space(config_.dimension);
    } else {
      logger_->error("Unknown metric: {}", config_.metric);
      return false;
    }

    // Load HNSW index
    auto* hnsw = new hnswlib::HierarchicalNSW<float>(
        static_cast<hnswlib::SpaceInterface<float>*>(space_), path);

    hnsw->setEf(config_.ef_search);
    index_ = hnsw;

    // Load ID mappings
    std::string mapping_path = path + ".mapping";
    std::ifstream ifs(mapping_path, std::ios::binary);
    if (!ifs) {
      logger_->error("Failed to open mapping file: {}", mapping_path);
      delete hnsw;
      delete static_cast<hnswlib::SpaceInterface<float>*>(space_);
      index_ = nullptr;
      space_ = nullptr;
      return false;
    }

    // Read number of mappings
    size_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

    // Read each mapping
    for (size_t i = 0; i < count; ++i) {
      size_t id_len = 0;
      ifs.read(reinterpret_cast<char*>(&id_len), sizeof(id_len));

      std::string id(id_len, '\0');
      ifs.read(&id[0], id_len);

      label_t label = 0;
      ifs.read(reinterpret_cast<char*>(&label), sizeof(label));

      id_to_label_[id] = label;
      label_to_id_[label] = id;
    }

    // Read next_label_
    ifs.read(reinterpret_cast<char*>(&next_label_), sizeof(next_label_));

    initialized_ = true;
    logger_->info("HNSW index loaded from: {} ({} vectors)", path,
                  hnsw->cur_element_count);
    return true;

  } catch (const std::exception& e) {
    logger_->error("Failed to load HNSW index: {}", e.what());
    if (index_) {
      delete static_cast<hnswlib::HierarchicalNSW<float>*>(index_);
      index_ = nullptr;
    }
    if (space_) {
      delete static_cast<hnswlib::SpaceInterface<float>*>(space_);
      space_ = nullptr;
    }
    return false;
  }
}

void HNSWIndex::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (index_) {
    delete static_cast<hnswlib::HierarchicalNSW<float>*>(index_);
    index_ = nullptr;
  }

  if (space_) {
    delete static_cast<hnswlib::SpaceInterface<float>*>(space_);
    space_ = nullptr;
  }

  id_to_label_.clear();
  label_to_id_.clear();
  next_label_ = 0;
  initialized_ = false;

  logger_->info("HNSW index cleared");
}

int HNSWIndex::GetSize() const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_ || !index_) {
    return 0;
  }

  auto* hnsw = static_cast<hnswlib::HierarchicalNSW<float>*>(index_);
  return static_cast<int>(hnsw->cur_element_count);
}

}  // namespace ravbot
