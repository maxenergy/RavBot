// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/vector_database.hpp"
#include "quantclaw/core/hnsw_index.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <filesystem>

namespace quantclaw {

VectorDatabase::VectorDatabase(const std::string& db_path,
                               std::shared_ptr<spdlog::logger> logger)
    : db_path_(db_path), logger_(logger) {
  // Use default config
  config_.use_hnsw = true;
  config_.hnsw_M = 16;
  config_.hnsw_ef_construction = 200;
  config_.hnsw_ef_search = 50;
  config_.max_elements = 100000;
  config_.metric = "cosine";
  use_hnsw_ = config_.use_hnsw;
}

VectorDatabase::VectorDatabase(const std::string& db_path,
                               std::shared_ptr<spdlog::logger> logger,
                               const VectorDatabaseConfig& config)
    : db_path_(db_path), logger_(logger), config_(config) {
  use_hnsw_ = config_.use_hnsw;
}

VectorDatabase::~VectorDatabase() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Save HNSW index before closing
  if (hnsw_index_ && hnsw_initialized_) {
    try {
      hnsw_index_->SaveIndex(GetIndexPath());
    } catch (const std::exception& e) {
      logger_->warn("Failed to save HNSW index on shutdown: {}", e.what());
    }
  }

  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool VectorDatabase::Initialize() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (db_) {
    return true;
  }

  int rc = sqlite3_open(db_path_.c_str(), &db_);
  if (rc != SQLITE_OK) {
    logger_->error("Failed to open vector database: {}", sqlite3_errmsg(db_));
    return false;
  }

  if (!LoadExtension()) {
    logger_->warn(
        "sqlite-vec extension not available, using brute-force fallback");
  }

  if (!CreateTables()) {
    logger_->error("Failed to create vector database tables");
    return false;
  }

  if (!LoadDimension()) {
    logger_->error("Failed to determine vector database dimension");
    return false;
  }

  // Initialize HNSW index if enabled
  if (use_hnsw_) {
    if (!InitializeHNSW()) {
      logger_->warn("Failed to initialize HNSW index, falling back to brute-force search");
      use_hnsw_ = false;
    }
  }

  logger_->info("Vector database initialized: {} (HNSW: {})", db_path_, use_hnsw_);
  return true;
}

bool VectorDatabase::LoadExtension() {
  if (!db_) {
    return false;
  }

  sqlite3_enable_load_extension(db_, 1);

  const char* extensions[] = {"vec0", "vector0", "sqlite-vec"};

  for (const char* ext : extensions) {
    char* err_msg = nullptr;
    int rc = sqlite3_load_extension(db_, ext, nullptr, &err_msg);
    if (rc == SQLITE_OK) {
      logger_->info("Loaded sqlite-vec extension: {}", ext);
      return true;
    }
    if (err_msg) {
      sqlite3_free(err_msg);
    }
  }

  return false;
}

bool VectorDatabase::CreateTables() {
  const char* sql = R"(
        CREATE TABLE IF NOT EXISTS vectors (
            id TEXT PRIMARY KEY,
            vector BLOB NOT NULL,
            text TEXT,
            metadata TEXT,
            timestamp INTEGER DEFAULT (strftime('%s', 'now'))
        );

        CREATE INDEX IF NOT EXISTS idx_vectors_timestamp ON vectors(timestamp);
    )";

  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    logger_->error("Failed to create tables: {}", err_msg ? err_msg : "");
    if (err_msg) {
      sqlite3_free(err_msg);
    }
    return false;
  }

  return true;
}

bool VectorDatabase::LoadDimension() {
  if (!db_) {
    return false;
  }

  const char* sql = "SELECT LENGTH(vector) FROM vectors LIMIT 1";
  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    logger_->error("Failed to prepare dimension query: {}",
                   sqlite3_errmsg(db_));
    return false;
  }

  dimension_ = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    int blob_size = sqlite3_column_int(stmt, 0);
    if (blob_size > 0 && blob_size % static_cast<int>(sizeof(float)) == 0) {
      dimension_ = blob_size / static_cast<int>(sizeof(float));
    }
  }

  sqlite3_finalize(stmt);
  return true;
}

bool VectorDatabase::IndexVector(const std::string& id,
                                 const std::vector<float>& vector,
                                 const std::string& text,
                                 const nlohmann::json& metadata) {
  logger_->debug("[IndexVector] START: id={}, vector_size={}", id, vector.size());
  std::lock_guard<std::mutex> lock(mutex_);
  logger_->debug("[IndexVector] Mutex acquired");

  if (!db_) {
    logger_->error("Vector database not initialized");
    return false;
  }
  if (id.empty()) {
    logger_->error("Cannot index vector with empty id");
    return false;
  }
  if (vector.empty()) {
    logger_->error("Cannot index empty vector");
    return false;
  }

  if (dimension_ == 0) {
    dimension_ = static_cast<int>(vector.size());
    logger_->info("Set vector dimension to: {}", dimension_);

    // Initialize HNSW now that we know the dimension
    if (use_hnsw_ && !hnsw_initialized_) {
      logger_->debug("[IndexVector] Calling InitializeHNSW...");
      if (!InitializeHNSW()) {
        logger_->warn("Failed to initialize HNSW index after setting dimension");
        use_hnsw_ = false;
      }
      logger_->debug("[IndexVector] InitializeHNSW returned");
    }
  } else if (static_cast<int>(vector.size()) != dimension_) {
    logger_->error("Vector dimension mismatch: expected {}, got {}",
                   dimension_, vector.size());
    return false;
  }

  logger_->debug("[IndexVector] Preparing SQLite insert...");
  std::vector<uint8_t> blob(vector.size() * sizeof(float));
  std::memcpy(blob.data(), vector.data(), blob.size());
  std::string metadata_json = metadata.dump();

  const char* sql =
      "INSERT OR REPLACE INTO vectors (id, vector, text, metadata) VALUES "
      "(?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;

  int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    logger_->error("Failed to prepare statement: {}", sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 2, blob.data(), static_cast<int>(blob.size()),
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, metadata_json.c_str(), -1, SQLITE_TRANSIENT);

  logger_->debug("[IndexVector] Executing SQLite insert...");
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  logger_->debug("[IndexVector] SQLite insert completed: rc={}", rc);

  if (rc != SQLITE_DONE) {
    logger_->error("Failed to insert vector: {}", sqlite3_errmsg(db_));
    return false;
  }

  // Add to HNSW index
  if (use_hnsw_ && hnsw_index_ && hnsw_initialized_) {
    logger_->debug("[IndexVector] Adding to HNSW index...");
    if (!hnsw_index_->AddVector(id, vector)) {
      logger_->warn("Failed to add vector to HNSW index: {}", id);
      // Continue anyway, SQLite has the data
    }
    logger_->debug("[IndexVector] HNSW add completed");
  }

  logger_->debug("[IndexVector] END: success");
  return true;
}

std::vector<VectorSearchResult> VectorDatabase::SearchVectors(
    const std::vector<float>& query_vector, int limit, float threshold) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<VectorSearchResult> results;

  if (!db_) {
    logger_->error("Vector database not initialized");
    return results;
  }
  if (query_vector.empty()) {
    logger_->error("Cannot search with empty vector");
    return results;
  }
  if (dimension_ != 0 && static_cast<int>(query_vector.size()) != dimension_) {
    logger_->warn("Query vector dimension mismatch: expected {}, got {}",
                  dimension_, query_vector.size());
    return results;
  }
  if (limit <= 0) {
    return results;
  }

  // Use HNSW if available
  if (use_hnsw_ && hnsw_index_ && hnsw_initialized_) {
    try {
      auto hnsw_results = hnsw_index_->Search(query_vector, limit);

      // Fetch metadata from SQLite for each result
      for (const auto& [id, distance] : hnsw_results) {
        // Apply threshold filter
        float similarity = 1.0f - distance;
        if (similarity < threshold) {
          continue;
        }

        // Fetch metadata from SQLite
        const char* sql = "SELECT text, metadata FROM vectors WHERE id = ?";
        sqlite3_stmt* stmt = nullptr;

        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
          logger_->warn("Failed to prepare statement for id {}: {}", id, sqlite3_errmsg(db_));
          continue;
        }

        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
          VectorSearchResult result;
          result.id = id;
          result.distance = distance;

          const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
          result.text = text ? text : "";

          const char* metadata_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
          if (metadata_str) {
            try {
              result.metadata = nlohmann::json::parse(metadata_str);
            } catch (...) {
              result.metadata = nlohmann::json::object();
            }
          } else {
            result.metadata = nlohmann::json::object();
          }

          results.push_back(std::move(result));
        }

        sqlite3_finalize(stmt);
      }

      return results;

    } catch (const std::exception& e) {
      logger_->error("HNSW search failed: {}, falling back to brute-force", e.what());
      // Fall through to brute-force search
    }
  }

  // Brute-force search fallback
  float norm_query = 0.0f;
  for (float value : query_vector) {
    norm_query += value * value;
  }
  if (norm_query <= std::numeric_limits<float>::epsilon()) {
    logger_->warn("Cannot search with zero-norm query vector");
    return results;
  }

  const char* sql = "SELECT id, vector, text, metadata FROM vectors";
  sqlite3_stmt* stmt = nullptr;

  int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    logger_->error("Failed to prepare search statement: {}",
                   sqlite3_errmsg(db_));
    return results;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* id =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const void* blob = sqlite3_column_blob(stmt, 1);
    int blob_size = sqlite3_column_bytes(stmt, 1);
    const char* text =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const char* metadata_str =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

    if (!blob || blob_size <= 0 ||
        blob_size % static_cast<int>(sizeof(float)) != 0) {
      continue;
    }

    std::vector<float> stored_vector(blob_size / static_cast<int>(sizeof(float)));
    std::memcpy(stored_vector.data(), blob, blob_size);
    if (stored_vector.size() != query_vector.size()) {
      continue;
    }

    float dot_product = 0.0f;
    float norm_stored = 0.0f;
    for (size_t i = 0; i < stored_vector.size(); ++i) {
      dot_product += query_vector[i] * stored_vector[i];
      norm_stored += stored_vector[i] * stored_vector[i];
    }
    if (norm_stored <= std::numeric_limits<float>::epsilon()) {
      continue;
    }

    float similarity = dot_product / (std::sqrt(norm_query) * std::sqrt(norm_stored));
    similarity = std::clamp(similarity, -1.0f, 1.0f);
    if (!std::isfinite(similarity) || similarity < threshold) {
      continue;
    }

    VectorSearchResult result;
    result.id = id ? id : "";
    result.distance = 1.0f - similarity;
    result.text = text ? text : "";
    result.metadata = nlohmann::json::object();
    if (metadata_str) {
      try {
        result.metadata = nlohmann::json::parse(metadata_str);
      } catch (...) {
        result.metadata = nlohmann::json::object();
      }
    }
    results.push_back(std::move(result));
  }

  sqlite3_finalize(stmt);

  std::sort(results.begin(), results.end(),
            [](const VectorSearchResult& a, const VectorSearchResult& b) {
              return a.distance < b.distance;
            });

  if (static_cast<int>(results.size()) > limit) {
    results.resize(limit);
  }

  return results;
}

bool VectorDatabase::DeleteVector(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_) {
    logger_->error("Vector database not initialized");
    return false;
  }

  const char* sql = "DELETE FROM vectors WHERE id = ?";
  sqlite3_stmt* stmt = nullptr;

  int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    logger_->error("Failed to prepare delete statement: {}", sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    logger_->error("Failed to delete vector: {}", sqlite3_errmsg(db_));
    return false;
  }

  if (sqlite3_changes(db_) == 0) {
    return false;
  }

  // Mark deleted in HNSW index
  if (use_hnsw_ && hnsw_index_ && hnsw_initialized_) {
    if (!hnsw_index_->MarkDeleted(id)) {
      logger_->warn("Failed to mark vector as deleted in HNSW index: {}", id);
      // Continue anyway, SQLite has deleted it
    }
  }

  const char* count_sql = "SELECT COUNT(*) FROM vectors";
  sqlite3_stmt* count_stmt = nullptr;
  int count_rc = sqlite3_prepare_v2(db_, count_sql, -1, &count_stmt, nullptr);
  if (count_rc == SQLITE_OK && sqlite3_step(count_stmt) == SQLITE_ROW &&
      sqlite3_column_int64(count_stmt, 0) == 0) {
    dimension_ = 0;
  }
  if (count_stmt) {
    sqlite3_finalize(count_stmt);
  }
  return true;
}

bool VectorDatabase::DeleteAll() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_) {
    logger_->error("Vector database not initialized");
    return false;
  }

  const char* sql = "DELETE FROM vectors";
  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    logger_->error("Failed to delete all vectors: {}", err_msg ? err_msg : "");
    if (err_msg) {
      sqlite3_free(err_msg);
    }
    return false;
  }

  dimension_ = 0;

  // Clear HNSW index
  if (use_hnsw_ && hnsw_index_ && hnsw_initialized_) {
    hnsw_index_->Clear();
    hnsw_initialized_ = false;
  }

  return true;
}

int64_t VectorDatabase::GetVectorCount() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_) {
    return 0;
  }

  const char* sql = "SELECT COUNT(*) FROM vectors";
  sqlite3_stmt* stmt = nullptr;

  int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    return 0;
  }

  int64_t count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int64(stmt, 0);
  }

  sqlite3_finalize(stmt);
  return count;
}

int VectorDatabase::GetDimension() {
  std::lock_guard<std::mutex> lock(mutex_);
  return dimension_;
}

// HNSW integration methods

std::string VectorDatabase::GetIndexPath() const {
  return db_path_ + ".hnsw";
}

bool VectorDatabase::InitializeHNSW() {
  logger_->debug("[InitializeHNSW] START: use_hnsw={}, dimension={}", use_hnsw_, dimension_);

  if (!use_hnsw_) {
    return false;
  }

  // If dimension is not set yet, defer initialization until first vector is indexed
  if (dimension_ == 0) {
    logger_->info("HNSW initialization deferred until first vector is indexed");
    return true;  // Return true to indicate HNSW is enabled, just not initialized yet
  }

  try {
    logger_->debug("[InitializeHNSW] Creating HNSW config...");
    // Create HNSW config
    HNSWConfig hnsw_config;
    hnsw_config.dimension = dimension_;
    hnsw_config.max_elements = config_.max_elements;
    hnsw_config.M = config_.hnsw_M;
    hnsw_config.ef_construction = config_.hnsw_ef_construction;
    hnsw_config.ef_search = config_.hnsw_ef_search;
    hnsw_config.metric = config_.metric;

    logger_->debug("[InitializeHNSW] Creating HNSW index object...");
    // Create HNSW index
    hnsw_index_ = std::make_unique<HNSWIndex>(hnsw_config, logger_);
    logger_->debug("[InitializeHNSW] HNSW index object created");

    // Try to load existing index
    std::string index_path = GetIndexPath();
    logger_->debug("[InitializeHNSW] Checking for existing index: {}", index_path);
    if (std::filesystem::exists(index_path)) {
      logger_->debug("[InitializeHNSW] Loading existing index...");
      if (hnsw_index_->LoadIndex(index_path)) {
        logger_->info("Loaded existing HNSW index from: {}", index_path);
        hnsw_initialized_ = true;
        return true;
      } else {
        logger_->warn("Failed to load HNSW index, will rebuild");
      }
    }

    logger_->debug("[InitializeHNSW] Initializing new HNSW index...");
    // Initialize new index
    if (!hnsw_index_->Initialize()) {
      logger_->error("Failed to initialize HNSW index");
      return false;
    }
    logger_->debug("[InitializeHNSW] HNSW index initialized");

    // Note: Don't call RebuildIndex() here as it would cause deadlock
    // when called from IndexVector() which already holds the mutex.
    // The index will be built incrementally as vectors are added.

    hnsw_initialized_ = true;
    logger_->info("HNSW index initialized successfully");
    return true;

  } catch (const std::exception& e) {
    logger_->error("Exception initializing HNSW: {}", e.what());
    return false;
  }
}

bool VectorDatabase::RebuildIndex() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!use_hnsw_) {
    logger_->error("HNSW not enabled");
    return false;
  }

  if (!db_) {
    logger_->error("Database not initialized");
    return false;
  }

  // If HNSW index doesn't exist yet, initialize it first
  if (!hnsw_index_) {
    if (dimension_ == 0) {
      logger_->error("Cannot rebuild HNSW index: dimension not set");
      return false;
    }
    if (!InitializeHNSW()) {
      logger_->error("Failed to initialize HNSW index for rebuild");
      return false;
    }
  }

  try {
    // Clear existing index
    hnsw_index_->Clear();
    if (!hnsw_index_->Initialize()) {
      logger_->error("Failed to reinitialize HNSW index");
      return false;
    }

    // Load all vectors from SQLite
    const char* sql = "SELECT id, vector FROM vectors";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      logger_->error("Failed to prepare rebuild query: {}", sqlite3_errmsg(db_));
      return false;
    }

    std::vector<std::pair<std::string, std::vector<float>>> batch;
    int count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      const void* blob = sqlite3_column_blob(stmt, 1);
      int blob_size = sqlite3_column_bytes(stmt, 1);

      if (!id || !blob || blob_size <= 0) {
        continue;
      }

      std::vector<float> vector(blob_size / sizeof(float));
      std::memcpy(vector.data(), blob, blob_size);

      batch.emplace_back(id, vector);
      count++;

      // Add in batches of 1000
      if (batch.size() >= 1000) {
        if (!hnsw_index_->AddVectorsBatch(batch)) {
          logger_->warn("Failed to add batch to HNSW index");
        }
        batch.clear();
      }
    }

    // Add remaining vectors
    if (!batch.empty()) {
      if (!hnsw_index_->AddVectorsBatch(batch)) {
        logger_->warn("Failed to add final batch to HNSW index");
      }
    }

    sqlite3_finalize(stmt);

    hnsw_initialized_ = true;
    logger_->info("Rebuilt HNSW index with {} vectors", count);
    return true;

  } catch (const std::exception& e) {
    logger_->error("Exception rebuilding HNSW index: {}", e.what());
    return false;
  }
}

bool VectorDatabase::SaveIndex() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!use_hnsw_) {
    logger_->warn("HNSW not enabled");
    return false;
  }

  if (!hnsw_index_ || !hnsw_initialized_) {
    logger_->warn("HNSW not initialized, nothing to save");
    return false;
  }

  try {
    std::string index_path = GetIndexPath();
    if (hnsw_index_->SaveIndex(index_path)) {
      logger_->info("Saved HNSW index to: {}", index_path);
      return true;
    } else {
      logger_->error("Failed to save HNSW index");
      return false;
    }
  } catch (const std::exception& e) {
    logger_->error("Exception saving HNSW index: {}", e.what());
    return false;
  }
}

bool VectorDatabase::LoadIndex() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!use_hnsw_ || !hnsw_index_) {
    logger_->warn("HNSW not enabled or not initialized");
    return false;
  }

  try {
    std::string index_path = GetIndexPath();
    if (!std::filesystem::exists(index_path)) {
      logger_->warn("HNSW index file not found: {}", index_path);
      return false;
    }

    if (hnsw_index_->LoadIndex(index_path)) {
      hnsw_initialized_ = true;
      logger_->info("Loaded HNSW index from: {}", index_path);
      return true;
    } else {
      logger_->error("Failed to load HNSW index");
      return false;
    }
  } catch (const std::exception& e) {
    logger_->error("Exception loading HNSW index: {}", e.what());
    return false;
  }
}

nlohmann::json VectorDatabase::GetIndexStats() {
  std::lock_guard<std::mutex> lock(mutex_);

  nlohmann::json stats;
  stats["use_hnsw"] = use_hnsw_;
  stats["hnsw_initialized"] = hnsw_initialized_;
  stats["dimension"] = dimension_;

  // Get vector count directly without calling GetVectorCount() to avoid deadlock
  int64_t count = 0;
  if (db_) {
    const char* sql = "SELECT COUNT(*) FROM vectors";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int64(stmt, 0);
    }
    if (stmt) {
      sqlite3_finalize(stmt);
    }
  }
  stats["vector_count"] = count;

  if (use_hnsw_ && hnsw_index_ && hnsw_initialized_) {
    stats["hnsw_size"] = hnsw_index_->GetSize();
    stats["hnsw_dimension"] = hnsw_index_->GetDimension();
    stats["hnsw_max_elements"] = hnsw_index_->GetMaxElements();
    stats["hnsw_M"] = config_.hnsw_M;
    stats["hnsw_ef_construction"] = config_.hnsw_ef_construction;
    stats["hnsw_ef_search"] = config_.hnsw_ef_search;
    stats["metric"] = config_.metric;
  }

  return stats;
}

}  // namespace quantclaw
