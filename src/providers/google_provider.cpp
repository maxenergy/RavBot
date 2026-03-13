// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/google_provider.hpp"

#include <sstream>
#include <stdexcept>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace quantclaw {

GoogleProvider::GoogleProvider(const std::string& api_key,
                               const std::string& base_url, int timeout,
                               std::shared_ptr<spdlog::logger> logger)
    : api_key_(api_key),
      base_url_(base_url.empty()
                    ? "https://generativelanguage.googleapis.com/v1beta"
                    : base_url),
      timeout_(timeout),
      logger_(logger) {}

std::string GoogleProvider::GetProviderName() const {
  return "google";
}

std::vector<std::string> GoogleProvider::GetSupportedModels() const {
  return {"gemini-2.0-flash-exp", "gemini-1.5-pro", "gemini-1.5-flash",
          "gemini-1.5-flash-8b",  "gemini-pro",     "gemini-pro-vision"};
}

nlohmann::json GoogleProvider::ConvertRequestToGemini(
    const ChatCompletionRequest& request) const {
  nlohmann::json gemini_request;

  // Convert messages to Gemini format
  nlohmann::json contents = nlohmann::json::array();

  for (const auto& msg : request.messages) {
    nlohmann::json content;
    nlohmann::json parts = nlohmann::json::array();

    // Get text content from ContentBlocks
    std::string text_content = msg.text();

    // Map roles: user->user, assistant->model, system->user (with prefix)
    if (msg.role == "system") {
      content["role"] = "user";
      nlohmann::json part = {{"text", "[System] " + text_content}};
      parts.push_back(part);
    } else if (msg.role == "assistant") {
      content["role"] = "model";
      nlohmann::json part = {{"text", text_content}};
      parts.push_back(part);
    } else {
      content["role"] = "user";
      nlohmann::json part = {{"text", text_content}};
      parts.push_back(part);
    }

    content["parts"] = parts;
    contents.push_back(content);
  }

  gemini_request["contents"] = contents;

  // Add generation config
  nlohmann::json generation_config;
  if (request.temperature > 0) {
    generation_config["temperature"] = request.temperature;
  }
  if (request.max_tokens > 0) {
    generation_config["maxOutputTokens"] = request.max_tokens;
  }

  if (!generation_config.empty()) {
    gemini_request["generationConfig"] = generation_config;
  }

  // Convert tools to Gemini function declarations
  if (!request.tools.empty()) {
    nlohmann::json tools = nlohmann::json::array();
    nlohmann::json function_declarations = nlohmann::json::array();

    for (const auto& tool : request.tools) {
      // Tools are already in JSON format
      if (tool.contains("type") && tool["type"] == "function") {
        nlohmann::json func_decl = {
            {"name", tool["function"]["name"]},
            {"description", tool["function"]["description"]}};

        if (tool["function"].contains("parameters")) {
          func_decl["parameters"] = tool["function"]["parameters"];
        }

        function_declarations.push_back(func_decl);
      }
    }

    if (!function_declarations.empty()) {
      tools.push_back({{"functionDeclarations", function_declarations}});
      gemini_request["tools"] = tools;
    }
  }

  return gemini_request;
}

ChatCompletionResponse
GoogleProvider::ConvertGeminiResponse(const nlohmann::json& response) const {
  ChatCompletionResponse result;

  try {
    if (!response.contains("candidates") || response["candidates"].empty()) {
      throw std::runtime_error("No candidates in Gemini response");
    }

    const auto& candidate = response["candidates"][0];
    const auto& content = candidate["content"];

    if (content.contains("parts") && !content["parts"].empty()) {
      const auto& part = content["parts"][0];

      // Check for text response
      if (part.contains("text")) {
        result.content = part["text"].get<std::string>();
        result.finish_reason = "stop";
      }

      // Check for function call
      if (part.contains("functionCall")) {
        const auto& func_call = part["functionCall"];
        result.finish_reason = "tool_calls";

        ToolCall tool_call;
        tool_call.id = "call_" + std::to_string(std::rand());
        tool_call.name = func_call["name"].get<std::string>();

        if (func_call.contains("args")) {
          tool_call.arguments = func_call["args"];
        }

        result.tool_calls.push_back(tool_call);
      }
    }

    // Extract usage information
    if (response.contains("usageMetadata")) {
      const auto& usage = response["usageMetadata"];
      result.usage.prompt_tokens = usage.value("promptTokenCount", 0);
      result.usage.completion_tokens = usage.value("candidatesTokenCount", 0);
      result.usage.total_tokens = usage.value("totalTokenCount", 0);
    }

  } catch (const std::exception& e) {
    logger_->error("Failed to parse Gemini response: {}", e.what());
    throw;
  }

  return result;
}

std::string GoogleProvider::MakeApiRequest(const std::string& model,
                                           const std::string& json_payload,
                                           bool stream) const {
  CurlHandle curl;
  if (!curl) {
    throw std::runtime_error("Failed to initialize CURL");
  }

  // Build URL with API key
  std::string url = base_url_ + "/models/" + model + ":";
  url += stream ? "streamGenerateContent" : "generateContent";
  url += "?key=" + api_key_;

  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, json_payload.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, timeout_);

  auto headers = CreateHeaders();
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

  std::string response_data;
  curl_easy_setopt(
      curl.get(), CURLOPT_WRITEFUNCTION,
      +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        auto* str = static_cast<std::string*>(userdata);
        str->append(ptr, size * nmemb);
        return size * nmemb;
      });
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_data);

  CURLcode res = curl_easy_perform(curl.get());
  if (res != CURLE_OK) {
    throw std::runtime_error(std::string("CURL request failed: ") +
                             curl_easy_strerror(res));
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code != 200) {
    logger_->error("Google API returned HTTP {}: {}", http_code, response_data);
    throw std::runtime_error("Google API request failed with HTTP " +
                             std::to_string(http_code));
  }

  return response_data;
}

CurlSlist GoogleProvider::CreateHeaders() const {
  CurlSlist headers;
  headers.append("Content-Type: application/json");
  return headers;
}

ChatCompletionResponse
GoogleProvider::ChatCompletion(const ChatCompletionRequest& request) {
  try {
    auto gemini_request = ConvertRequestToGemini(request);
    std::string json_payload = gemini_request.dump();

    logger_->debug("Google API request: {}", json_payload);

    std::string response_str =
        MakeApiRequest(request.model, json_payload, false);

    logger_->debug("Google API response: {}", response_str);

    auto response_json = nlohmann::json::parse(response_str);
    return ConvertGeminiResponse(response_json);

  } catch (const std::exception& e) {
    logger_->error("Google ChatCompletion failed: {}", e.what());
    throw;
  }
}

void GoogleProvider::ChatCompletionStream(
    const ChatCompletionRequest& request,
    std::function<void(const ChatCompletionResponse&)> callback) {
  try {
    auto gemini_request = ConvertRequestToGemini(request);
    std::string json_payload = gemini_request.dump();

    logger_->debug("Google API streaming request: {}", json_payload);

    // For streaming, we need to handle SSE-like responses
    // Simplified implementation: make non-streaming request and call callback
    // once
    std::string response_str =
        MakeApiRequest(request.model, json_payload, false);

    auto response_json = nlohmann::json::parse(response_str);
    auto response = ConvertGeminiResponse(response_json);

    callback(response);

    ChatCompletionResponse end_response;
    end_response.is_stream_end = true;
    callback(end_response);

  } catch (const std::exception& e) {
    logger_->error("Google ChatCompletionStream failed: {}", e.what());
    throw;
  }
}

}  // namespace quantclaw
