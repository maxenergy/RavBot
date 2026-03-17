// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/core/vector_database.hpp"

#include <iostream>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

using namespace ravbot;

int main() {
  auto logger = spdlog::stdout_color_mt("test");
  logger->set_level(spdlog::level::info);

  std::string db_path = "/tmp/test_vector_db_simple.db";
  std::filesystem::remove(db_path);
  std::filesystem::remove(db_path + ".hnsw");
  std::filesystem::remove(db_path + ".hnsw.mapping");

  VectorDatabaseConfig config;
  config.use_hnsw = true;

  VectorDatabase db(db_path, logger, config);

  std::cout << "1. Initializing database..." << std::endl;
  if (!db.Initialize()) {
    std::cerr << "Failed to initialize database" << std::endl;
    return 1;
  }

  std::cout << "2. IsUsingHNSW: " << (db.IsUsingHNSW() ? "true" : "false") << std::endl;

  std::cout << "3. Adding vectors..." << std::endl;
  for (int i = 0; i < 10; ++i) {
    std::vector<float> vec(128, static_cast<float>(i));
    if (!db.IndexVector("doc" + std::to_string(i), vec, "text" + std::to_string(i))) {
      std::cerr << "Failed to index vector " << i << std::endl;
      return 1;
    }
  }

  std::cout << "4. Vector count: " << db.GetVectorCount() << std::endl;

  std::cout << "5. Searching..." << std::endl;
  std::vector<float> query(128, 5.0f);
  auto results = db.SearchVectors(query, 3, 0.0f);
  std::cout << "   Found " << results.size() << " results" << std::endl;
  for (const auto& result : results) {
    std::cout << "   - " << result.id << " (distance: " << result.distance << ")" << std::endl;
  }

  std::cout << "6. Rebuilding index..." << std::endl;
  if (!db.RebuildIndex()) {
    std::cerr << "Failed to rebuild index" << std::endl;
    return 1;
  }

  std::cout << "7. Saving index..." << std::endl;
  if (!db.SaveIndex()) {
    std::cerr << "Failed to save index" << std::endl;
    return 1;
  }

  std::cout << "8. Getting stats..." << std::endl;
  auto stats = db.GetIndexStats();
  std::cout << "   use_hnsw: " << stats["use_hnsw"].get<bool>() << std::endl;
  std::cout << "   hnsw_initialized: " << stats["hnsw_initialized"].get<bool>() << std::endl;
  std::cout << "   dimension: " << stats["dimension"].get<int>() << std::endl;
  std::cout << "   vector_count: " << stats["vector_count"].get<int64_t>() << std::endl;

  std::cout << "\nAll tests passed!" << std::endl;

  // Cleanup
  std::filesystem::remove(db_path);
  std::filesystem::remove(db_path + ".hnsw");
  std::filesystem::remove(db_path + ".hnsw.mapping");

  return 0;
}
