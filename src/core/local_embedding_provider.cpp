// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/local_embedding_provider.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <sstream>
#include <unordered_set>

namespace quantclaw {

LocalEmbeddingProvider::LocalEmbeddingProvider(
    int dimension,
    std::shared_ptr<spdlog::logger> logger)
    : dimension_(dimension),
      logger_(logger ? logger : spdlog::default_logger()),
      is_trained_(false) {
  InitializeDefaultProjection();
}

std::vector<float> LocalEmbeddingProvider::Embed(const std::string& text) {
  auto tfidf = ComputeTFIDF(text);
  auto embedding = ProjectToLSA(tfidf);
  Normalize(embedding);
  return embedding;
}

std::vector<std::vector<float>> LocalEmbeddingProvider::EmbedBatch(
    const std::vector<std::string>& texts) {
  std::vector<std::vector<float>> results;
  results.reserve(texts.size());
  for (const auto& text : texts) {
    results.push_back(Embed(text));
  }
  return results;
}

void LocalEmbeddingProvider::Train(const std::vector<std::string>& corpus) {
  if (corpus.empty()) {
    logger_->warn("Cannot train on empty corpus");
    return;
  }

  logger_->info("Training local embedding model on {} documents", corpus.size());

  // Build vocabulary and document frequency
  std::unordered_map<std::string, int> document_frequency;
  std::unordered_set<std::string> all_words;

  for (const auto& doc : corpus) {
    auto tokens = Tokenize(doc);
    std::unordered_set<std::string> unique_words(tokens.begin(), tokens.end());
    for (const auto& word : unique_words) {
      document_frequency[word]++;
      all_words.insert(word);
    }
  }

  // Build vocabulary (only words that appear in at least 2 documents)
  vocabulary_.clear();
  int vocab_index = 0;
  for (const auto& word : all_words) {
    if (document_frequency[word] >= 2) {
      vocabulary_[word] = vocab_index++;
    }
  }

  logger_->info("Vocabulary size: {}", vocabulary_.size());

  // Compute IDF scores
  idf_scores_.clear();
  int num_docs = static_cast<int>(corpus.size());
  for (const auto& [word, df] : document_frequency) {
    if (vocabulary_.count(word) > 0) {
      // IDF = log(N / df)
      idf_scores_[word] = std::log(static_cast<float>(num_docs) / static_cast<float>(df));
    }
  }

  // Build TF-IDF matrix (num_docs x vocab_size)
  std::vector<std::vector<float>> tfidf_matrix;
  tfidf_matrix.reserve(corpus.size());

  for (const auto& doc : corpus) {
    tfidf_matrix.push_back(ComputeTFIDF(doc));
  }

  // Perform SVD-like dimensionality reduction (simplified LSA)
  // For simplicity, we use random projection with normalization
  size_t vocab_size = vocabulary_.size();
  projection_matrix_.clear();
  projection_matrix_.resize(vocab_size);

  std::mt19937 rng(42);  // Fixed seed for reproducibility
  std::normal_distribution<float> dist(0.0f, 1.0f / std::sqrt(static_cast<float>(dimension_)));

  for (size_t i = 0; i < vocab_size; ++i) {
    projection_matrix_[i].resize(dimension_);
    for (int j = 0; j < dimension_; ++j) {
      projection_matrix_[i][j] = dist(rng);
    }
  }

  // Orthogonalize projection matrix using Gram-Schmidt
  for (int j = 0; j < dimension_; ++j) {
    // Extract column j
    std::vector<float> col_j(vocab_size);
    for (size_t i = 0; i < vocab_size; ++i) {
      col_j[i] = projection_matrix_[i][j];
    }

    // Orthogonalize against previous columns
    for (int k = 0; k < j; ++k) {
      std::vector<float> col_k(vocab_size);
      for (size_t i = 0; i < vocab_size; ++i) {
        col_k[i] = projection_matrix_[i][k];
      }

      // Compute dot product
      float dot = 0.0f;
      for (size_t i = 0; i < vocab_size; ++i) {
        dot += col_j[i] * col_k[i];
      }

      // Subtract projection
      for (size_t i = 0; i < vocab_size; ++i) {
        col_j[i] -= dot * col_k[i];
      }
    }

    // Normalize
    float norm = 0.0f;
    for (size_t i = 0; i < vocab_size; ++i) {
      norm += col_j[i] * col_j[i];
    }
    norm = std::sqrt(norm);

    if (norm > 1e-6f) {
      for (size_t i = 0; i < vocab_size; ++i) {
        projection_matrix_[i][j] = col_j[i] / norm;
      }
    }
  }

  is_trained_ = true;
  logger_->info("Training complete");
}

std::vector<std::string> LocalEmbeddingProvider::Tokenize(const std::string& text) const {
  std::vector<std::string> tokens;
  std::string word;

  for (char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      word += std::tolower(static_cast<unsigned char>(c));
    } else if (!word.empty()) {
      tokens.push_back(word);
      word.clear();
    }
  }

  if (!word.empty()) {
    tokens.push_back(word);
  }

  return tokens;
}

std::unordered_map<std::string, float> LocalEmbeddingProvider::ComputeTF(
    const std::vector<std::string>& tokens) const {
  std::unordered_map<std::string, float> tf;

  for (const auto& token : tokens) {
    tf[token] += 1.0f;
  }

  // Normalize by document length
  float doc_length = static_cast<float>(tokens.size());
  if (doc_length > 0.0f) {
    for (auto& [word, freq] : tf) {
      freq /= doc_length;
    }
  }

  return tf;
}

std::vector<float> LocalEmbeddingProvider::ComputeTFIDF(const std::string& text) const {
  auto tokens = Tokenize(text);
  auto tf = ComputeTF(tokens);

  // Create TF-IDF vector
  std::vector<float> tfidf_vector(vocabulary_.size(), 0.0f);

  for (const auto& [word, tf_value] : tf) {
    auto vocab_it = vocabulary_.find(word);
    if (vocab_it != vocabulary_.end()) {
      int index = vocab_it->second;
      auto idf_it = idf_scores_.find(word);
      float idf_value = (idf_it != idf_scores_.end()) ? idf_it->second : 1.0f;
      tfidf_vector[index] = tf_value * idf_value;
    }
  }

  return tfidf_vector;
}

std::vector<float> LocalEmbeddingProvider::ProjectToLSA(
    const std::vector<float>& tfidf_vector) const {
  std::vector<float> embedding(dimension_, 0.0f);

  if (tfidf_vector.size() != projection_matrix_.size()) {
    logger_->warn("TF-IDF vector size mismatch: {} vs {}",
                  tfidf_vector.size(), projection_matrix_.size());
    return embedding;
  }

  // Matrix multiplication: embedding = projection_matrix^T * tfidf_vector
  for (size_t i = 0; i < projection_matrix_.size(); ++i) {
    for (int j = 0; j < dimension_; ++j) {
      embedding[j] += tfidf_vector[i] * projection_matrix_[i][j];
    }
  }

  return embedding;
}

void LocalEmbeddingProvider::Normalize(std::vector<float>& vector) const {
  float norm = 0.0f;
  for (float v : vector) {
    norm += v * v;
  }
  norm = std::sqrt(norm);

  if (norm > 1e-6f) {
    for (float& v : vector) {
      v /= norm;
    }
  } else {
    // If vector is all zeros, set to uniform distribution
    float uniform_value = 1.0f / std::sqrt(static_cast<float>(vector.size()));
    for (float& v : vector) {
      v = uniform_value;
    }
  }
}

void LocalEmbeddingProvider::InitializeDefaultProjection() {
  // Initialize with a small default vocabulary for untrained use
  std::vector<std::string> common_words = {
    "the", "be", "to", "of", "and", "a", "in", "that", "have", "i",
    "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
    "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
    "or", "an", "will", "my", "one", "all", "would", "there", "their", "what"
  };

  vocabulary_.clear();
  for (size_t i = 0; i < common_words.size(); ++i) {
    vocabulary_[common_words[i]] = static_cast<int>(i);
  }

  // Initialize IDF scores (all equal for default)
  idf_scores_.clear();
  for (const auto& word : common_words) {
    idf_scores_[word] = 1.0f;
  }

  // Initialize random projection matrix
  size_t vocab_size = vocabulary_.size();
  projection_matrix_.clear();
  projection_matrix_.resize(vocab_size);

  std::mt19937 rng(42);
  std::normal_distribution<float> dist(0.0f, 1.0f / std::sqrt(static_cast<float>(dimension_)));

  for (size_t i = 0; i < vocab_size; ++i) {
    projection_matrix_[i].resize(dimension_);
    for (int j = 0; j < dimension_; ++j) {
      projection_matrix_[i][j] = dist(rng);
    }
  }

  logger_->info("Initialized default projection with {} common words", vocab_size);
}

}  // namespace quantclaw
