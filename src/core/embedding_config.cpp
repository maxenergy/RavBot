// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/embedding_config.hpp"
#include "quantclaw/core/mock_embedding_provider.hpp"

#include <fstream>
#include <regex>
#include <cstdlib>

namespace quantclaw {

std::shared_ptr<EmbeddingProviderRegistry> EmbeddingConfig::LoadFromFile(
    const std::string& config_path,
    std::shared_ptr<spdlog::logger> logger) {
  if (!logger) {
    logger = spdlog::default_logger();
  }

  std::ifstream file(config_path);
  if (!file.is_open()) {
    logger->error("Failed to open config file: {}", config_path);
    return nullptr;
  }

  nlohmann::json config;
  try {
    file >> config;
  } catch (const nlohmann::json::parse_error& e) {
    logger->error("Failed to parse config file: {}", e.what());
    return nullptr;
  }

  return LoadFromJson(config, logger);
}

std::shared_ptr<EmbeddingProviderRegistry> EmbeddingConfig::LoadFromJson(
    const nlohmann::json& config,
    std::shared_ptr<spdlog::logger> logger) {
  if (!logger) {
    logger = spdlog::default_logger();
  }

  auto registry = std::make_shared<EmbeddingProviderRegistry>();

  if (!config.contains("embedding")) {
    logger->warn("No 'embedding' section in config");
    return registry;
  }

  auto embedding_config = config["embedding"];

  // Load providers
  if (embedding_config.contains("providers")) {
    for (auto& [name, provider_config] : embedding_config["providers"].items()) {
      try {
        std::string type = provider_config.value("type", "");
        if (type.empty()) {
          logger->warn("Provider '{}' has no type, skipping", name);
          continue;
        }

        auto provider = CreateProvider(type, provider_config, logger);
        if (provider) {
          registry->RegisterProvider(name, provider);
          logger->info("Registered embedding provider: {} ({})", name, type);
        }
      } catch (const std::exception& e) {
        logger->error("Failed to create provider '{}': {}", name, e.what());
      }
    }
  }

  // Set default provider
  if (embedding_config.contains("default_provider")) {
    std::string default_provider = embedding_config["default_provider"];
    if (registry->HasProvider(default_provider)) {
      registry->SetDefaultProvider(default_provider);
      logger->info("Set default embedding provider: {}", default_provider);
    } else {
      logger->warn("Default provider '{}' not found", default_provider);
    }
  }

  return registry;
}

std::shared_ptr<EmbeddingProvider> EmbeddingConfig::CreateProvider(
    const std::string& type,
    const nlohmann::json& config,
    std::shared_ptr<spdlog::logger> logger) {
  if (!logger) {
    logger = spdlog::default_logger();
  }

  if (type == "mock") {
    int dimension = config.value("dimension", 384);
    return std::make_shared<MockEmbeddingProvider>(dimension);
  }

  if (type == "local") {
    int dimension = config.value("dimension", 384);
    auto provider = std::make_shared<LocalEmbeddingProvider>(dimension, logger);

    // Load training corpus if specified
    if (config.contains("corpus_file")) {
      std::string corpus_file = ExpandEnvVars(config["corpus_file"]);
      std::ifstream file(corpus_file);
      if (file.is_open()) {
        std::vector<std::string> corpus;
        std::string line;
        while (std::getline(file, line)) {
          if (!line.empty()) {
            corpus.push_back(line);
          }
        }
        provider->Train(corpus);
        logger->info("Trained local provider on {} documents", corpus.size());
      } else {
        logger->warn("Failed to open corpus file: {}", corpus_file);
      }
    }

    return provider;
  }

  if (type == "openai") {
    std::string api_key = ExpandEnvVars(config.value("api_key", ""));
    std::string model = config.value("model", "text-embedding-3-small");

    if (api_key.empty()) {
      logger->warn("OpenAI API key is empty");
    }

    auto provider = std::make_shared<OpenAIEmbeddingProvider>(api_key, model, logger);

    if (config.contains("batch_size")) {
      provider->SetBatchSize(config["batch_size"]);
    }

    if (config.contains("endpoint")) {
      provider->SetEndpoint(ExpandEnvVars(config["endpoint"]));
    }

    return provider;
  }

  if (type == "anthropic" || type == "voyage") {
    std::string api_key = ExpandEnvVars(config.value("api_key", ""));
    std::string model = config.value("model", "voyage-3");

    if (api_key.empty()) {
      logger->warn("Anthropic/Voyage API key is empty");
    }

    auto provider = std::make_shared<AnthropicEmbeddingProvider>(api_key, model, logger);

    if (config.contains("batch_size")) {
      provider->SetBatchSize(config["batch_size"]);
    }

    if (config.contains("endpoint")) {
      provider->SetEndpoint(ExpandEnvVars(config["endpoint"]));
    }

    if (config.contains("input_type")) {
      provider->SetInputType(config["input_type"]);
    }

    return provider;
  }

  if (type == "ollama") {
    std::string model = config.value("model", "nomic-embed-text");
    std::string endpoint = ExpandEnvVars(config.value("endpoint", "http://localhost:11434"));

    auto provider = std::make_shared<OllamaEmbeddingProvider>(model, endpoint, logger);

    if (config.contains("timeout")) {
      provider->SetTimeout(config["timeout"]);
    }

    return provider;
  }

  logger->error("Unknown provider type: {}", type);
  return nullptr;
}

std::string EmbeddingConfig::GetEnvVar(const std::string& var_name,
                                       const std::string& default_value) {
  const char* value = std::getenv(var_name.c_str());
  return value ? std::string(value) : default_value;
}

std::string EmbeddingConfig::ExpandEnvVars(const std::string& str) {
  std::string result = str;
  std::regex env_var_pattern(R"(\$\{([^}]+)\})");
  std::smatch match;

  while (std::regex_search(result, match, env_var_pattern)) {
    std::string var_name = match[1].str();
    std::string var_value = GetEnvVar(var_name, "");
    result.replace(match.position(), match.length(), var_value);
  }

  return result;
}

}  // namespace quantclaw
