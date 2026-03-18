// Copyright 2026 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/providers/llama_cpp_mobile_provider.hpp"

#include <filesystem>
#include <sstream>
#include <utility>

#include "ravbot/config.hpp"

namespace ravbot {

namespace {

constexpr bool kLlamaCppBackendLinked =
#ifdef RAVBOT_ENABLE_LLAMA_CPP_MOBILE_BACKEND
    true;
#else
    false;
#endif
    ;

std::string ResolveModelPath(const std::string& configured_path,
                             const std::string& models_dir) {
  if (configured_path.empty()) {
    return {};
  }

  std::filesystem::path resolved(
      RavBotConfig::ExpandHome(configured_path));
  if (resolved.is_relative() && !models_dir.empty()) {
    resolved = std::filesystem::path(models_dir) / resolved;
  }
  return resolved.lexically_normal().string();
}

bool PathExists(const std::string& path) {
  return !path.empty() && std::filesystem::exists(std::filesystem::path(path));
}

std::string BuildDetailMessage(
    const LlamaCppMobileProvider::RuntimeStatus& status) {
  if (status.llm_model_path.empty()) {
    return "No local llama.cpp text model is configured for mobile runtime.";
  }
  if (!status.llm_model_exists) {
    return "Configured llama.cpp text model is missing: " +
           status.llm_model_path;
  }
  if (!status.backend_linked) {
    return "llama.cpp mobile backend is not linked into this build.";
  }
  if (!status.vision_model_path.empty() && !status.vision_model_exists) {
    return "Text model is present, but vision model is missing: " +
           status.vision_model_path;
  }
  if (!status.mmproj_path.empty() && !status.mmproj_exists) {
    return "Text model is present, but mmproj projector is missing: " +
           status.mmproj_path;
  }
  if (status.vision_ready) {
    return "llama.cpp mobile runtime is ready for local text and vision.";
  }
  return "llama.cpp mobile runtime is ready for local text inference.";
}

}  // namespace

nlohmann::json LlamaCppMobileProvider::RuntimeStatus::ToJson() const {
  return {{"provider", "llama_cpp_mobile"},
          {"runtime", "llama.cpp"},
          {"ready", ready},
          {"textReady", text_ready},
          {"visionReady", vision_ready},
          {"backendLinked", backend_linked},
          {"llmModelPath", llm_model_path},
          {"llmModelExists", llm_model_exists},
          {"visionModelPath", vision_model_path},
          {"visionModelExists", vision_model_exists},
          {"mmprojPath", mmproj_path},
          {"mmprojExists", mmproj_exists},
          {"modelsDir", models_dir},
          {"vulkanRequested", vulkan_requested},
          {"vulkanEnabled", vulkan_enabled},
          {"detail", detail}};
}

LlamaCppMobileProvider::LlamaCppMobileProvider(
    Options options,
    std::shared_ptr<spdlog::logger> logger)
    : options_(std::move(options)),
      logger_(logger ? logger : spdlog::default_logger()),
      runtime_status_(BuildRuntimeStatus()) {
  if (logger_) {
    logger_->info("Configured llama.cpp mobile provider: {}",
                  runtime_status_.detail);
  }
}

ChatCompletionResponse LlamaCppMobileProvider::ChatCompletion(
    const ChatCompletionRequest& request) {
  (void)request;
  if (logger_) {
    logger_->warn("LlamaCppMobileProvider fallback response: {}",
                  runtime_status_.detail);
  }
  ChatCompletionResponse response;
  std::ostringstream content;
  content << runtime_status_.detail;
  if (!runtime_status_.models_dir.empty()) {
    content << " modelsDir=" << runtime_status_.models_dir;
  }
  response.content = content.str();
  response.finish_reason = "error";
  return response;
}

void LlamaCppMobileProvider::ChatCompletionStream(
    const ChatCompletionRequest& request,
    std::function<void(const ChatCompletionResponse&)> callback) {
  callback(ChatCompletion(request));
}

std::vector<std::string> LlamaCppMobileProvider::GetSupportedModels() const {
  std::vector<std::string> models;
  if (!runtime_status_.llm_model_path.empty()) {
    models.push_back(runtime_status_.llm_model_path);
  }
  if (!runtime_status_.vision_model_path.empty()) {
    models.push_back(runtime_status_.vision_model_path);
  }
  return models;
}

LlamaCppMobileProvider::RuntimeStatus
LlamaCppMobileProvider::BuildRuntimeStatus() const {
  RuntimeStatus status;
  status.backend_linked = kLlamaCppBackendLinked;
  status.vulkan_requested = options_.enable_vulkan;
  status.vulkan_enabled = kLlamaCppBackendLinked && options_.enable_vulkan;
  status.models_dir = ResolveModelPath(options_.models_dir, "");
  status.llm_model_path =
      ResolveModelPath(options_.model_path, status.models_dir);
  status.vision_model_path =
      ResolveModelPath(options_.vision_model_path, status.models_dir);
  status.mmproj_path =
      ResolveModelPath(options_.mmproj_path, status.models_dir);
  status.llm_model_exists = PathExists(status.llm_model_path);
  status.vision_model_exists = PathExists(status.vision_model_path);
  status.mmproj_exists = PathExists(status.mmproj_path);
  status.text_ready = status.backend_linked && status.llm_model_exists;
  const bool vision_assets_ready =
      !status.vision_model_path.empty() && status.vision_model_exists &&
      !status.mmproj_path.empty() && status.mmproj_exists;
  status.vision_ready = status.backend_linked && vision_assets_ready;
  status.ready = status.text_ready;
  status.detail = BuildDetailMessage(status);
  return status;
}

}  // namespace ravbot
