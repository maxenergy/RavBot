// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ravbot/core/embedding_provider.hpp"
#include "ravbot/core/local_embedding_provider.hpp"
#include "ravbot/core/openai_embedding_provider.hpp"
#include "ravbot/core/anthropic_embedding_provider.hpp"
#include "ravbot/core/ollama_embedding_provider.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <memory>
#include <string>

namespace ravbot {

// Configuration loader for embedding providers
class EmbeddingConfig {
 public:
  // Load configuration from JSON file
  static std::shared_ptr<EmbeddingProviderRegistry> LoadFromFile(
      const std::string& config_path,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Load configuration from JSON object
  static std::shared_ptr<EmbeddingProviderRegistry> LoadFromJson(
      const nlohmann::json& config,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Create provider from configuration
  static std::shared_ptr<EmbeddingProvider> CreateProvider(
      const std::string& type,
      const nlohmann::json& config,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Get environment variable with fallback
  static std::string GetEnvVar(const std::string& var_name,
                               const std::string& default_value = "");

  // Expand environment variables in string (e.g., "${OPENAI_API_KEY}")
  static std::string ExpandEnvVars(const std::string& str);
};

}  // namespace ravbot
