// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "quantclaw/core/embedding_provider.hpp"

#include <spdlog/spdlog.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace quantclaw {

// Local Embedding Provider using TF-IDF + LSA (Latent Semantic Analysis)
// Provides real embeddings without external API calls
class LocalEmbeddingProvider : public EmbeddingProvider {
 public:
  explicit LocalEmbeddingProvider(int dimension = 384,
                                 std::shared_ptr<spdlog::logger> logger = nullptr);
  ~LocalEmbeddingProvider() override = default;

  std::vector<float> Embed(const std::string& text) override;

  std::vector<std::vector<float>> EmbedBatch(
      const std::vector<std::string>& texts) override;

  int GetDimension() const override { return dimension_; }

  std::string GetName() const override { return "local"; }

  std::string GetModel() const override { return "tfidf-lsa"; }

  bool IsAvailable() const override { return true; }

  // Train the model on a corpus of documents
  void Train(const std::vector<std::string>& corpus);

  // Get vocabulary size
  size_t GetVocabularySize() const { return vocabulary_.size(); }

  // Check if model is trained
  bool IsTrained() const { return is_trained_; }

 private:
  int dimension_;
  std::shared_ptr<spdlog::logger> logger_;
  bool is_trained_;

  // Vocabulary: word -> index
  std::unordered_map<std::string, int> vocabulary_;

  // IDF scores: word -> idf value
  std::unordered_map<std::string, float> idf_scores_;

  // LSA projection matrix (vocabulary_size x dimension)
  std::vector<std::vector<float>> projection_matrix_;

  // Tokenize text into words
  std::vector<std::string> Tokenize(const std::string& text) const;

  // Compute TF (term frequency) for a document
  std::unordered_map<std::string, float> ComputeTF(
      const std::vector<std::string>& tokens) const;

  // Compute TF-IDF vector
  std::vector<float> ComputeTFIDF(const std::string& text) const;

  // Project TF-IDF vector to lower dimension using LSA
  std::vector<float> ProjectToLSA(const std::vector<float>& tfidf_vector) const;

  // Normalize vector to unit length
  void Normalize(std::vector<float>& vector) const;

  // Initialize with default projection (random)
  void InitializeDefaultProjection();
};

}  // namespace quantclaw
