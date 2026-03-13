// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <memory>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw {

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

class VectorDatabase {
 public:
  VectorDatabase(const std::string& db_path,
                 std::shared_ptr<spdlog::logger> logger);
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

 private:
  bool CreateTables();
  bool LoadDimension();
  bool LoadExtension();

  std::string db_path_;
  std::shared_ptr<spdlog::logger> logger_;
  sqlite3* db_ = nullptr;
  int dimension_ = 0;
  std::mutex mutex_;
};

}  // namespace quantclaw
