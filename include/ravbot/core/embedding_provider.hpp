// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace ravbot {

// Base class for embedding providers
class EmbeddingProvider {
 public:
  virtual ~EmbeddingProvider() = default;

  // Embed a single text into a vector
  virtual std::vector<float> Embed(const std::string& text) = 0;

  // Embed multiple texts into vectors (batch processing)
  virtual std::vector<std::vector<float>> EmbedBatch(
      const std::vector<std::string>& texts) = 0;

  // Get the dimension of the embedding vectors
  virtual int GetDimension() const = 0;

  // Get the name of the provider
  virtual std::string GetName() const = 0;

  // Get the model name (optional)
  virtual std::string GetModel() const { return ""; }

  // Check if the provider is available (e.g., API key is set)
  virtual bool IsAvailable() const { return true; }
};

// Registry for managing embedding providers
class EmbeddingProviderRegistry {
 public:
  EmbeddingProviderRegistry() = default;
  ~EmbeddingProviderRegistry() = default;

  // Register a provider
  void RegisterProvider(const std::string& name,
                       std::shared_ptr<EmbeddingProvider> provider);

  // Unregister a provider
  void UnregisterProvider(const std::string& name);

  // Get a provider by name
  std::shared_ptr<EmbeddingProvider> GetProvider(const std::string& name);

  // List all registered providers
  std::vector<std::string> ListProviders() const;

  // Set the default provider
  void SetDefaultProvider(const std::string& name);

  // Get the default provider
  std::shared_ptr<EmbeddingProvider> GetDefaultProvider();

  // Check if a provider is registered
  bool HasProvider(const std::string& name) const;

 private:
  std::unordered_map<std::string, std::shared_ptr<EmbeddingProvider>> providers_;
  std::string default_provider_;
  mutable std::mutex mutex_;
};

}  // namespace ravbot
