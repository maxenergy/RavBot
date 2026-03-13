// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/vector_database.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace quantclaw {

VectorDatabase::VectorDatabase(const std::string& db_path,
                               std::shared_ptr<spdlog::logger> logger)
    : db_path_(db_path), logger_(logger) {}

VectorDatabase::~VectorDatabase() {
  std::lock_guard<std::mutex> lock(mutex_);
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

  logger_->info("Vector database initialized: {}", db_path_);
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
  std::lock_guard<std::mutex> lock(mutex_);

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
  } else if (static_cast<int>(vector.size()) != dimension_) {
    logger_->error("Vector dimension mismatch: expected {}, got {}",
                   dimension_, vector.size());
    return false;
  }

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

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    logger_->error("Failed to insert vector: {}", sqlite3_errmsg(db_));
    return false;
  }

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

}  // namespace quantclaw
