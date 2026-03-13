// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/qwen_provider.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace quantclaw {

namespace {

struct PendingToolCall {
  std::string id;
  std::string name;
  std::string arguments;
};

std::string trim_copy(std::string value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

ChatCompletionResponse
ParseQwenJsonResponse(const nlohmann::json& response_json) {
  ChatCompletionResponse response;

  if (response_json.contains("choices") && !response_json["choices"].empty()) {
    const auto& choice = response_json["choices"][0];
    const auto& message = choice["message"];

    if (message.contains("content") && !message["content"].is_null()) {
      response.content = message["content"].get<std::string>();
    }

    if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
      for (const auto& tc : message["tool_calls"]) {
        ToolCall tool_call;
        tool_call.id = tc.value("id", "");
        tool_call.name = tc["function"]["name"].get<std::string>();
        tool_call.arguments = tc["function"]["arguments"];
        response.tool_calls.push_back(tool_call);
      }
      response.finish_reason = "tool_calls";
    } else {
      response.finish_reason = choice.value("finish_reason", "stop");
    }
  }

  if (response_json.contains("usage")) {
    const auto& usage = response_json["usage"];
    response.usage.prompt_tokens = usage.value("prompt_tokens", 0);
    response.usage.completion_tokens = usage.value("completion_tokens", 0);
    response.usage.total_tokens = usage.value("total_tokens", 0);
  }

  return response;
}

void EmitPendingToolCalls(
    std::vector<PendingToolCall>* pending_tool_calls,
    const std::function<void(const ChatCompletionResponse&)>& callback) {
  if (pending_tool_calls->empty()) {
    return;
  }

  ChatCompletionResponse response;
  response.finish_reason = "tool_calls";
  for (const auto& pending : *pending_tool_calls) {
    ToolCall tool_call;
    tool_call.id = pending.id;
    tool_call.name = pending.name;
    tool_call.arguments =
        nlohmann::json::parse(pending.arguments, nullptr, false);
    if (tool_call.arguments.is_discarded()) {
      tool_call.arguments = nlohmann::json::object();
    }
    response.tool_calls.push_back(tool_call);
  }

  pending_tool_calls->clear();
  callback(response);
}

void ParseQwenSseResponse(
    const std::string& response_str,
    const std::function<void(const ChatCompletionResponse&)>& callback,
    const std::shared_ptr<spdlog::logger>& logger) {
  std::istringstream stream(response_str);
  std::string line;
  std::vector<PendingToolCall> pending_tool_calls;
  TokenUsage final_usage;
  bool saw_done = false;

  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("data:", 0) != 0) {
      continue;
    }

    std::string data = trim_copy(line.substr(5));
    if (data.empty()) {
      continue;
    }
    if (data == "[DONE]") {
      saw_done = true;
      break;
    }

    auto chunk_json = nlohmann::json::parse(data, nullptr, false);
    if (chunk_json.is_discarded()) {
      logger->warn("Skipping malformed Qwen SSE chunk: {}", data);
      continue;
    }

    if (chunk_json.contains("usage")) {
      const auto& usage = chunk_json["usage"];
      final_usage.prompt_tokens = usage.value("prompt_tokens", 0);
      final_usage.completion_tokens = usage.value("completion_tokens", 0);
      final_usage.total_tokens = usage.value("total_tokens", 0);
    }

    if (!chunk_json.contains("choices") || chunk_json["choices"].empty()) {
      continue;
    }

    const auto& choice = chunk_json["choices"][0];
    if (choice.contains("delta") && choice["delta"].is_object()) {
      const auto& delta = choice["delta"];

      if (delta.contains("content") && !delta["content"].is_null()) {
        ChatCompletionResponse response;
        response.content = delta["content"].get<std::string>();
        callback(response);
      }

      if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        for (const auto& tc_delta : delta["tool_calls"]) {
          int index = tc_delta.value("index", 0);
          while (static_cast<int>(pending_tool_calls.size()) <= index) {
            pending_tool_calls.push_back({});
          }

          auto& pending = pending_tool_calls[index];
          if (tc_delta.contains("id") && !tc_delta["id"].is_null()) {
            pending.id = tc_delta["id"].get<std::string>();
          }
          if (tc_delta.contains("function") &&
              tc_delta["function"].is_object()) {
            const auto& fn = tc_delta["function"];
            if (fn.contains("name") && !fn["name"].is_null()) {
              pending.name = fn["name"].get<std::string>();
            }
            if (fn.contains("arguments") && !fn["arguments"].is_null()) {
              pending.arguments += fn["arguments"].get<std::string>();
            }
          }
        }
      }
    }

    std::string finish_reason;
    if (choice.contains("finish_reason") &&
        !choice["finish_reason"].is_null()) {
      finish_reason = choice["finish_reason"].get<std::string>();
    }
    if (finish_reason == "tool_calls") {
      EmitPendingToolCalls(&pending_tool_calls, callback);
    }
  }

  if (!pending_tool_calls.empty()) {
    EmitPendingToolCalls(&pending_tool_calls, callback);
  }

  if (!saw_done) {
    logger->warn("Qwen streaming response ended without [DONE] marker");
  }

  ChatCompletionResponse end_response;
  end_response.is_stream_end = true;
  end_response.usage = final_usage;
  callback(end_response);
}

}  // namespace

QwenProvider::QwenProvider(const std::string& api_key,
                           const std::string& base_url, int timeout,
                           std::shared_ptr<spdlog::logger> logger)
    : api_key_(api_key),
      base_url_(base_url.empty()
                    ? "https://dashscope.aliyuncs.com/compatible-mode/v1"
                    : base_url),
      timeout_(timeout),
      logger_(logger) {}

std::string QwenProvider::GetProviderName() const {
  return "qwen";
}

std::vector<std::string> QwenProvider::GetSupportedModels() const {
  return {"qwen-turbo",
          "qwen-plus",
          "qwen-max",
          "qwen-max-longcontext",
          "qwen-vl-plus",
          "qwen-vl-max",
          "qwen2.5-72b-instruct",
          "qwen2.5-32b-instruct",
          "qwen2.5-14b-instruct",
          "qwen2.5-7b-instruct"};
}

std::string QwenProvider::MakeApiRequest(const std::string& json_payload,
                                         bool stream) const {
  (void)stream;
  CurlHandle curl;
  if (!curl) {
    throw std::runtime_error("Failed to initialize CURL");
  }

  std::string url = base_url_ + "/chat/completions";
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
    logger_->error("Qwen API returned HTTP {}: {}", http_code, response_data);
    throw std::runtime_error("Qwen API request failed with HTTP " +
                             std::to_string(http_code));
  }

  return response_data;
}

CurlSlist QwenProvider::CreateHeaders() const {
  CurlSlist headers;
  headers.append("Content-Type: application/json");
  headers.append(("Authorization: Bearer " + api_key_).c_str());
  return headers;
}

ChatCompletionResponse
QwenProvider::ChatCompletion(const ChatCompletionRequest& request) {
  try {
    // Build OpenAI-compatible request
    nlohmann::json req = {{"model", request.model},
                          {"messages", nlohmann::json::array()},
                          {"stream", false}};

    // Convert messages
    for (const auto& msg : request.messages) {
      nlohmann::json message = {{"role", msg.role}, {"content", msg.text()}};
      req["messages"].push_back(message);
    }

    // Add optional parameters
    if (request.temperature > 0) {
      req["temperature"] = request.temperature;
    }
    if (request.max_tokens > 0) {
      req["max_tokens"] = request.max_tokens;
    }

    // Add tools if present
    if (!request.tools.empty()) {
      req["tools"] = request.tools;
    }

    std::string json_payload = req.dump();
    logger_->debug("Qwen API request: {}", json_payload);

    std::string response_str = MakeApiRequest(json_payload, false);
    logger_->debug("Qwen API response: {}", response_str);

    auto response_json = nlohmann::json::parse(response_str);
    return ParseQwenJsonResponse(response_json);

  } catch (const std::exception& e) {
    logger_->error("Qwen ChatCompletion failed: {}", e.what());
    throw;
  }
}

void QwenProvider::ChatCompletionStream(
    const ChatCompletionRequest& request,
    std::function<void(const ChatCompletionResponse&)> callback) {
  try {
    // Build request with stream=true
    nlohmann::json req = {{"model", request.model},
                          {"messages", nlohmann::json::array()},
                          {"stream", true}};

    // Convert messages
    for (const auto& msg : request.messages) {
      nlohmann::json message = {{"role", msg.role}, {"content", msg.text()}};
      req["messages"].push_back(message);
    }

    // Add optional parameters
    if (request.temperature > 0) {
      req["temperature"] = request.temperature;
    }
    if (request.max_tokens > 0) {
      req["max_tokens"] = request.max_tokens;
    }

    // Add tools if present
    if (!request.tools.empty()) {
      req["tools"] = request.tools;
    }

    std::string json_payload = req.dump();
    logger_->debug("Qwen API streaming request: {}", json_payload);

    std::string response_str = MakeApiRequest(json_payload, true);
    std::string trimmed = trim_copy(response_str);

    if (!trimmed.empty() && trimmed.front() == '{') {
      auto response_json = nlohmann::json::parse(trimmed);
      callback(ParseQwenJsonResponse(response_json));

      ChatCompletionResponse end_response;
      end_response.is_stream_end = true;
      callback(end_response);
      return;
    }

    ParseQwenSseResponse(response_str, callback, logger_);

  } catch (const std::exception& e) {
    logger_->error("Qwen ChatCompletionStream failed: {}", e.what());
    throw;
  }
}

}  // namespace quantclaw
