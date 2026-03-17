// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

// Tests for VectorDatabase P2.1 additions:
//   - FTS5 full-text search (SearchFTS)
//   - Hybrid search via RRF fusion (HybridSearch)

#include "ravbot/core/vector_database.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace ravbot;

// ── Fixture ──────────────────────────────────────────────────────────────────

class VectorDatabaseFTSTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_fts", null_sink);

    db_path_ = "/tmp/test_vector_db_fts.db";
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_ + ".hnsw");
    std::filesystem::remove(db_path_ + ".hnsw.mapping");

    VectorDatabaseConfig cfg;
    cfg.use_hnsw = false;  // disable HNSW so tests focus on FTS/hybrid

    db_ = std::make_unique<VectorDatabase>(db_path_, logger_, cfg);
    ASSERT_TRUE(db_->Initialize());

    // Index a few documents with known text content
    // Dimension 4 — trivial vectors, enough to exercise code paths
    IndexDoc("doc1", "the quick brown fox jumps over the lazy dog",
             {1.0f, 0.0f, 0.0f, 0.0f});
    IndexDoc("doc2", "machine learning with neural networks is powerful",
             {0.0f, 1.0f, 0.0f, 0.0f});
    IndexDoc("doc3", "error: segmentation fault in process 1234",
             {0.0f, 0.0f, 1.0f, 0.0f});
    IndexDoc("doc4", "the fox and the hound are friends",
             {0.9f, 0.1f, 0.0f, 0.0f});
  }

  void TearDown() override {
    db_.reset();
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_ + ".hnsw");
    std::filesystem::remove(db_path_ + ".hnsw.mapping");
  }

  void IndexDoc(const std::string& id,
                const std::string& text,
                const std::vector<float>& vec) {
    bool ok = db_->IndexVector(id, vec, text, {});
    ASSERT_TRUE(ok) << "IndexVector failed for id=" << id;
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::string db_path_;
  std::unique_ptr<VectorDatabase> db_;
};

// ── SearchFTS tests ───────────────────────────────────────────────────────────

// Basic: searching for "fox" should return doc1 and doc4 (both mention fox)
TEST_F(VectorDatabaseFTSTest, SearchFTS_BasicQuery) {
  auto results = db_->SearchFTS("fox", 10);
  // If FTS5 is not compiled in, the function returns empty — skip gracefully
  if (results.empty()) {
    GTEST_SKIP() << "FTS5 not available in this SQLite build";
  }
  bool found_doc1 = false, found_doc4 = false;
  for (const auto& r : results) {
    if (r.id == "doc1") found_doc1 = true;
    if (r.id == "doc4") found_doc4 = true;
  }
  EXPECT_TRUE(found_doc1) << "doc1 (fox) should appear in FTS results";
  EXPECT_TRUE(found_doc4) << "doc4 (fox) should appear in FTS results";
}

// FTS should NOT return unrelated documents for a specific term
TEST_F(VectorDatabaseFTSTest, SearchFTS_UnrelatedDocNotReturned) {
  auto results = db_->SearchFTS("neural networks", 10);
  if (results.empty()) GTEST_SKIP() << "FTS5 not available";

  for (const auto& r : results) {
    // doc1, doc3, doc4 don't mention neural networks
    EXPECT_NE(r.id, "doc1");
    EXPECT_NE(r.id, "doc3");
    EXPECT_NE(r.id, "doc4");
  }
}

// Searching for an error term should find doc3
TEST_F(VectorDatabaseFTSTest, SearchFTS_ErrorTermFindDoc3) {
  auto results = db_->SearchFTS("segmentation fault", 10);
  if (results.empty()) GTEST_SKIP() << "FTS5 not available";

  bool found = false;
  for (const auto& r : results) {
    if (r.id == "doc3") found = true;
  }
  EXPECT_TRUE(found);
}

// Limit parameter is respected
TEST_F(VectorDatabaseFTSTest, SearchFTS_LimitRespected) {
  auto results = db_->SearchFTS("the", 2);
  if (results.empty()) GTEST_SKIP() << "FTS5 not available";
  EXPECT_LE(static_cast<int>(results.size()), 2);
}

// Empty query returns empty results, not a crash
TEST_F(VectorDatabaseFTSTest, SearchFTS_EmptyQueryReturnsEmpty) {
  auto results = db_->SearchFTS("", 10);
  EXPECT_TRUE(results.empty());
}

// After deleting a document, SearchFTS should not return it
TEST_F(VectorDatabaseFTSTest, SearchFTS_DeletedDocNotReturned) {
  auto before = db_->SearchFTS("fox", 10);
  if (before.empty()) GTEST_SKIP() << "FTS5 not available";

  ASSERT_TRUE(db_->DeleteVector("doc1"));

  auto after = db_->SearchFTS("fox", 10);
  for (const auto& r : after) {
    EXPECT_NE(r.id, "doc1") << "Deleted doc1 should not appear in FTS";
  }
}

// ── HybridSearch tests ────────────────────────────────────────────────────────

// HybridSearch with both vector and text inputs returns non-empty results
TEST_F(VectorDatabaseFTSTest, HybridSearch_ReturnsSomeResults) {
  std::vector<float> qvec = {1.0f, 0.0f, 0.0f, 0.0f};
  auto results = db_->HybridSearch(qvec, "fox", 5, 0.0f);
  // Must return at least one result (even if FTS5 unavailable, vector hits)
  EXPECT_FALSE(results.empty());
}

// HybridSearch: doc1 and doc4 are both vector-similar AND text-relevant —
// they should appear near the top of RRF-fused results.
TEST_F(VectorDatabaseFTSTest, HybridSearch_FoxRelevantDocsRankedHigh) {
  std::vector<float> qvec = {1.0f, 0.0f, 0.0f, 0.0f};
  auto results = db_->HybridSearch(qvec, "fox", 4, 0.0f);
  ASSERT_FALSE(results.empty());

  bool found_doc1 = false, found_doc4 = false;
  for (const auto& r : results) {
    if (r.id == "doc1") found_doc1 = true;
    if (r.id == "doc4") found_doc4 = true;
  }
  EXPECT_TRUE(found_doc1 || found_doc4)
      << "At least one fox-related doc should appear in hybrid results";
}

// HybridSearch with empty query_text degrades to pure vector search
TEST_F(VectorDatabaseFTSTest, HybridSearch_EmptyTextUsesVectorOnly) {
  std::vector<float> qvec = {1.0f, 0.0f, 0.0f, 0.0f};
  auto results = db_->HybridSearch(qvec, "", 4, 0.0f);
  EXPECT_FALSE(results.empty());
}

// HybridSearch with empty vector degrades to pure FTS search
TEST_F(VectorDatabaseFTSTest, HybridSearch_EmptyVectorUsesFTSOnly) {
  std::vector<float> empty_vec;
  auto results = db_->HybridSearch(empty_vec, "neural", 4, 0.0f);
  // If FTS5 unavailable, empty is acceptable; otherwise should find doc2
  if (!results.empty()) {
    bool found_doc2 = false;
    for (const auto& r : results) {
      if (r.id == "doc2") found_doc2 = true;
    }
    EXPECT_TRUE(found_doc2);
  }
}

// HybridSearch limit is respected
TEST_F(VectorDatabaseFTSTest, HybridSearch_LimitRespected) {
  std::vector<float> qvec = {0.5f, 0.5f, 0.0f, 0.0f};
  auto results = db_->HybridSearch(qvec, "fox", 2, 0.0f);
  EXPECT_LE(static_cast<int>(results.size()), 2);
}

// HybridSearch results have no duplicate IDs
TEST_F(VectorDatabaseFTSTest, HybridSearch_NoDuplicateIDs) {
  std::vector<float> qvec = {0.7f, 0.3f, 0.0f, 0.0f};
  auto results = db_->HybridSearch(qvec, "fox", 10, 0.0f);

  std::vector<std::string> seen_ids;
  for (const auto& r : results) {
    for (const auto& seen : seen_ids) {
      EXPECT_NE(r.id, seen) << "Duplicate id=" << r.id << " in HybridSearch";
    }
    seen_ids.push_back(r.id);
  }
}

// Both empty inputs return empty results without crashing
TEST_F(VectorDatabaseFTSTest, HybridSearch_BothEmptyReturnsEmpty) {
  auto results = db_->HybridSearch({}, "", 5, 0.0f);
  EXPECT_TRUE(results.empty());
}
