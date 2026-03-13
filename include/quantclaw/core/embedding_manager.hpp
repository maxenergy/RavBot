// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace quantclaw {

struct EmbeddingRequest {
    std::string model;
    std::vector<std::string> texts;
    std::string encoding_format = "float";  // "float" or "base64"
};

struct EmbeddingResult {
    std::vector<std::vector<float>> embeddings;
    int total_tokens = 0;
};

class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;

    virtual std::string GetProviderName() const = 0;
    virtual std::vector<std::string> GetSupportedModels() const = 0;

    virtual EmbeddingResult GenerateEmbeddings(const EmbeddingRequest& request) = 0;
};

class EmbeddingManager {
public:
    EmbeddingManager(std::shared_ptr<spdlog::logger> logger);

    // Register embedding providers
    void RegisterProvider(const std::string& provider_id,
                          std::shared_ptr<EmbeddingProvider> provider);

    // Generate embeddings
    EmbeddingResult GenerateEmbeddings(const std::string& provider_id,
                                       const EmbeddingRequest& request);

    // Get available providers and models
    std::vector<std::string> GetProviderIds() const;
    std::vector<std::string> GetSupportedModels(const std::string& provider_id) const;

private:
    std::shared_ptr<spdlog::logger> logger_;
    std::unordered_map<std::string, std::shared_ptr<EmbeddingProvider>> providers_;
};

} // namespace quantclaw
