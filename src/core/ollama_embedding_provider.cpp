// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/ollama_embedding_provider.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace quantclaw {

namespace {

// Callback for libcurl to write response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total_size = size * nmemb;
  auto* str = static_cast<std::string*>(userp);
  str->append(static_cast<char*>(contents), total_size);
  return total_size;
}

}  // namespace

OllamaEmbeddingProvider::OllamaEmbeddingProvider(
    const std::string& model,
    const std::string& endpoint,
    std::shared_ptr<spdlog::logger> logger)
    : model_(model),
      endpoint_(endpoint),
      timeout_(30),
      dimension_(0),
      logger_(logger ? logger : spdlog::default_logger()) {
  // Detect dimension on first use
}

std::vector<float> OllamaEmbeddingProvider::Embed(const std::string& text) {
  auto results = EmbedBatch({text});
  if (results.empty()) {
    throw std::runtime_error("Failed to generate embedding");
  }
  return results[0];
}

std::vector<std::vector<float>> OllamaEmbeddingProvider::EmbedBatch(
    const std::vector<std::string>& texts) {
  if (texts.empty()) {
    return {};
  }

  return MakeRequest(texts);
}

int OllamaEmbeddingProvider::GetDimension() const {
  if (dimension_ == 0) {
    // Lazy dimension detection
    const_cast<OllamaEmbeddingProvider*>(this)->DetectDimension();
  }
  return dimension_;
}

bool OllamaEmbeddingProvider::IsAvailable() const {
  return CheckAvailability();
}

void OllamaEmbeddingProvider::SetEndpoint(const std::string& endpoint) {
  endpoint_ = endpoint;
  dimension_ = 0;  // Reset dimension cache
}

void OllamaEmbeddingProvider::SetModel(const std::string& model) {
  model_ = model;
  dimension_ = 0;  // Reset dimension cache
}

void OllamaEmbeddingProvider::SetTimeout(int timeout_seconds) {
  timeout_ = timeout_seconds;
}

std::vector<std::vector<float>> OllamaEmbeddingProvider::MakeRequest(
    const std::vector<std::string>& texts) {
  std::vector<std::vector<float>> results;
  results.reserve(texts.size());

  // Ollama API processes one text at a time
  for (const auto& text : texts) {
    CURL* curl = curl_easy_init();
    if (!curl) {
      throw std::runtime_error("Failed to initialize CURL");
    }

    // Build request JSON
    json request_body;
    request_body["model"] = model_;
    request_body["prompt"] = text;

    std::string request_str = request_body.dump();
    std::string response_str;

    // Set up headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // Configure CURL
    std::string url = endpoint_ + "/api/embeddings";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_));

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
      throw std::runtime_error("CURL request failed: " + std::string(curl_easy_strerror(res)));
    }

    if (http_code != 200) {
      logger_->error("Ollama API returned HTTP {}: {}", http_code, response_str);
      throw std::runtime_error("Ollama API request failed with HTTP " + std::to_string(http_code));
    }

    // Parse response
    json response;
    try {
      response = json::parse(response_str);
    } catch (const json::parse_error& e) {
      throw std::runtime_error("Failed to parse Ollama API response: " + std::string(e.what()));
    }

    // Extract embedding
    if (!response.contains("embedding") || !response["embedding"].is_array()) {
      throw std::runtime_error("Invalid response format: missing 'embedding' array");
    }

    std::vector<float> embedding;
    for (const auto& value : response["embedding"]) {
      embedding.push_back(value.get<float>());
    }

    // Cache dimension on first embedding
    if (dimension_ == 0 && !embedding.empty()) {
      dimension_ = static_cast<int>(embedding.size());
      logger_->info("Detected Ollama embedding dimension: {}", dimension_);
    }

    results.push_back(std::move(embedding));
  }

  return results;
}

void OllamaEmbeddingProvider::DetectDimension() {
  try {
    // Generate a test embedding to detect dimension
    auto test_embedding = Embed("test");
    dimension_ = static_cast<int>(test_embedding.size());
    logger_->info("Detected Ollama embedding dimension: {}", dimension_);
  } catch (const std::exception& e) {
    logger_->warn("Failed to detect dimension: {}", e.what());
    // Use common default for nomic-embed-text
    dimension_ = 768;
  }
}

bool OllamaEmbeddingProvider::CheckAvailability() const {
  CURL* curl = curl_easy_init();
  if (!curl) {
    return false;
  }

  std::string response_str;
  std::string url = endpoint_ + "/api/tags";

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  curl_easy_cleanup(curl);

  if (res != CURLE_OK || http_code != 200) {
    return false;
  }

  // Check if model is available
  try {
    json response = json::parse(response_str);
    if (response.contains("models") && response["models"].is_array()) {
      for (const auto& model : response["models"]) {
        if (model.contains("name")) {
          std::string model_name = model["name"].get<std::string>();
          // Match exact name or name with :tag suffix
          if (model_name == model_ ||
              model_name.find(model_ + ":") == 0) {
            return true;
          }
        }
      }
    }
  } catch (...) {
    return false;
  }

  return false;
}

}  // namespace quantclaw
