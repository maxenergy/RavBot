// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/openai_embedding_provider.hpp"

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

OpenAIEmbeddingProvider::OpenAIEmbeddingProvider(
    const std::string& api_key,
    const std::string& model,
    std::shared_ptr<spdlog::logger> logger)
    : api_key_(api_key),
      model_(model),
      endpoint_("https://api.openai.com/v1/embeddings"),
      batch_size_(100),
      dimension_(GetModelDimension(model)),
      logger_(logger ? logger : spdlog::default_logger()) {
  if (api_key_.empty()) {
    logger_->warn("OpenAI API key is empty");
  }
}

std::vector<float> OpenAIEmbeddingProvider::Embed(const std::string& text) {
  auto results = EmbedBatch({text});
  if (results.empty()) {
    throw std::runtime_error("Failed to generate embedding");
  }
  return results[0];
}

std::vector<std::vector<float>> OpenAIEmbeddingProvider::EmbedBatch(
    const std::vector<std::string>& texts) {
  if (texts.empty()) {
    return {};
  }

  std::vector<std::vector<float>> all_results;
  all_results.reserve(texts.size());

  // Process in batches
  for (size_t i = 0; i < texts.size(); i += batch_size_) {
    size_t end = std::min(i + batch_size_, texts.size());
    std::vector<std::string> batch(texts.begin() + i, texts.begin() + end);

    auto batch_results = MakeRequest(batch);
    all_results.insert(all_results.end(), batch_results.begin(), batch_results.end());
  }

  return all_results;
}

int OpenAIEmbeddingProvider::GetDimension() const {
  return dimension_;
}

void OpenAIEmbeddingProvider::SetBatchSize(int batch_size) {
  if (batch_size <= 0 || batch_size > 2048) {
    throw std::invalid_argument("Batch size must be between 1 and 2048");
  }
  batch_size_ = batch_size;
}

void OpenAIEmbeddingProvider::SetEndpoint(const std::string& endpoint) {
  endpoint_ = endpoint;
}

std::vector<std::vector<float>> OpenAIEmbeddingProvider::MakeRequest(
    const std::vector<std::string>& texts) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Failed to initialize CURL");
  }

  // Build request JSON
  json request_body;
  request_body["model"] = model_;
  request_body["input"] = texts;
  request_body["encoding_format"] = "float";

  std::string request_str = request_body.dump();
  std::string response_str;

  // Set up headers
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string auth_header = "Authorization: Bearer " + api_key_;
  headers = curl_slist_append(headers, auth_header.c_str());

  // Configure CURL
  curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

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
    logger_->error("OpenAI API returned HTTP {}: {}", http_code, response_str);
    throw std::runtime_error("OpenAI API request failed with HTTP " + std::to_string(http_code));
  }

  // Parse response
  json response;
  try {
    response = json::parse(response_str);
  } catch (const json::parse_error& e) {
    throw std::runtime_error("Failed to parse OpenAI API response: " + std::string(e.what()));
  }

  // Extract embeddings
  std::vector<std::vector<float>> results;
  if (!response.contains("data") || !response["data"].is_array()) {
    throw std::runtime_error("Invalid response format: missing 'data' array");
  }

  auto data = response["data"];
  results.reserve(data.size());

  for (const auto& item : data) {
    if (!item.contains("embedding") || !item["embedding"].is_array()) {
      throw std::runtime_error("Invalid response format: missing 'embedding' array");
    }

    std::vector<float> embedding;
    for (const auto& value : item["embedding"]) {
      embedding.push_back(value.get<float>());
    }

    if (static_cast<int>(embedding.size()) != dimension_) {
      logger_->warn("Embedding dimension mismatch: expected {}, got {}",
                    dimension_, embedding.size());
    }

    results.push_back(std::move(embedding));
  }

  return results;
}

int OpenAIEmbeddingProvider::GetModelDimension(const std::string& model) {
  if (model == "text-embedding-3-small") {
    return 1536;
  } else if (model == "text-embedding-3-large") {
    return 3072;
  } else if (model == "text-embedding-ada-002") {
    return 1536;
  } else {
    // Default to 1536 for unknown models
    return 1536;
  }
}

}  // namespace quantclaw
