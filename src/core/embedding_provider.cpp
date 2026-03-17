// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/core/embedding_provider.hpp"

#include <algorithm>

namespace ravbot {

void EmbeddingProviderRegistry::RegisterProvider(
    const std::string& name,
    std::shared_ptr<EmbeddingProvider> provider) {
  std::lock_guard<std::mutex> lock(mutex_);
  providers_[name] = provider;

  // Set as default if it's the first provider
  if (default_provider_.empty()) {
    default_provider_ = name;
  }
}

void EmbeddingProviderRegistry::UnregisterProvider(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  providers_.erase(name);

  // Clear default if it was the unregistered provider
  if (default_provider_ == name) {
    default_provider_.clear();
    if (!providers_.empty()) {
      default_provider_ = providers_.begin()->first;
    }
  }
}

std::shared_ptr<EmbeddingProvider> EmbeddingProviderRegistry::GetProvider(
    const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = providers_.find(name);
  if (it != providers_.end()) {
    return it->second;
  }
  return nullptr;
}

std::vector<std::string> EmbeddingProviderRegistry::ListProviders() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> names;
  names.reserve(providers_.size());
  for (const auto& [name, _] : providers_) {
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

void EmbeddingProviderRegistry::SetDefaultProvider(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (providers_.find(name) != providers_.end()) {
    default_provider_ = name;
  }
}

std::shared_ptr<EmbeddingProvider> EmbeddingProviderRegistry::GetDefaultProvider() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (default_provider_.empty() || providers_.find(default_provider_) == providers_.end()) {
    return nullptr;
  }
  return providers_[default_provider_];
}

bool EmbeddingProviderRegistry::HasProvider(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return providers_.find(name) != providers_.end();
}

}  // namespace ravbot
