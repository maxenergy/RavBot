// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>

#include "ravbot/providers/google_provider.hpp"
#include "ravbot/providers/llama_cpp_mobile_provider.hpp"
#include "ravbot/providers/provider_registry.hpp"
#include "ravbot/providers/qwen_provider.hpp"

#include <gtest/gtest.h>

namespace ravbot {

static std::shared_ptr<spdlog::logger> make_logger(const std::string& name) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, null_sink);
}

// --- ModelRef tests ---

TEST(ModelRefTest, ParseWithProvider) {
  auto ref = ModelRef::parse("anthropic/claude-opus-4-6");
  EXPECT_EQ(ref.provider, "anthropic");
  EXPECT_EQ(ref.model, "claude-opus-4-6");
}

TEST(ModelRefTest, ParseWithoutProvider) {
  auto ref = ModelRef::parse("gpt-4o", "openai");
  EXPECT_EQ(ref.provider, "openai");
  EXPECT_EQ(ref.model, "gpt-4o");
}

TEST(ModelRefTest, ToString) {
  ModelRef ref;
  ref.provider = "anthropic";
  ref.model = "claude-opus-4-6";
  EXPECT_EQ(ref.to_string(), "anthropic/claude-opus-4-6");
}

TEST(ModelRefTest, ParseWithDefaultProvider) {
  auto ref = ModelRef::parse("qwen-max", "qwen");
  EXPECT_EQ(ref.provider, "qwen");
  EXPECT_EQ(ref.model, "qwen-max");
}

// --- ProviderRegistry tests ---

TEST(ProviderRegistryTest, RegisterBuiltinFactories) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  // Should have factories but no entries yet
  EXPECT_FALSE(reg->HasProvider("nonexistent"));
}

TEST(ProviderRegistryTest, AddProviderEntry) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  ProviderEntry entry;
  entry.id = "openai";
  entry.api_key = "test-key";
  entry.base_url = "https://api.openai.com/v1";
  reg->AddProvider(entry);

  EXPECT_TRUE(reg->HasProvider("openai"));
  auto* e = reg->GetEntry("openai");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->api_key, "test-key");
}

TEST(ProviderRegistryTest, LoadFromConfig) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  nlohmann::json config = {
      {"openai",
       {{"apiKey", "sk-test"},
        {"baseUrl", "https://api.openai.com/v1"},
        {"timeout", 60}}},
      {"anthropic", {{"apiKey", "ak-test"}, {"timeout", 45}}},
  };
  reg->LoadFromConfig(config);

  EXPECT_TRUE(reg->HasProvider("openai"));
  EXPECT_TRUE(reg->HasProvider("anthropic"));

  auto ids = reg->ProviderIds();
  EXPECT_EQ(ids.size(), 2);
}

TEST(ProviderRegistryTest, ModelAliases) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));

  reg->AddAlias("opus", "anthropic/claude-opus-4-6");
  reg->AddAlias("gpt4", "openai/gpt-4o");

  auto ref = reg->ResolveModel("opus");
  EXPECT_EQ(ref.provider, "anthropic");
  EXPECT_EQ(ref.model, "claude-opus-4-6");

  ref = reg->ResolveModel("gpt4");
  EXPECT_EQ(ref.provider, "openai");
  EXPECT_EQ(ref.model, "gpt-4o");

  // Non-aliased should pass through
  ref = reg->ResolveModel("openai/gpt-3.5-turbo");
  EXPECT_EQ(ref.provider, "openai");
  EXPECT_EQ(ref.model, "gpt-3.5-turbo");
}

TEST(ProviderRegistryTest, LoadAliasesFromJson) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));

  nlohmann::json aliases = {
      {"anthropic/claude-opus-4-6", {{"alias", "opus"}}},
      {"openai/gpt-4o", {{"alias", "gpt4"}}},
  };
  reg->LoadAliases(aliases);

  auto all = reg->Aliases();
  EXPECT_EQ(all.size(), 2);

  auto ref = reg->ResolveModel("opus");
  EXPECT_EQ(ref.model, "claude-opus-4-6");
}

TEST(ProviderRegistryTest, GetProviderCreatesInstance) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  ProviderEntry entry;
  entry.id = "openai";
  entry.api_key = "test-key";
  reg->AddProvider(entry);

  auto provider = reg->GetProvider("openai");
  ASSERT_NE(provider, nullptr);
  EXPECT_EQ(provider->GetProviderName(), "openai");

  // Second call returns same instance
  auto provider2 = reg->GetProvider("openai");
  EXPECT_EQ(provider.get(), provider2.get());
}

TEST(ProviderRegistryTest, GetProviderForModel) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  ProviderEntry entry;
  entry.id = "anthropic";
  entry.api_key = "test-key";
  reg->AddProvider(entry);

  auto ref = ModelRef::parse("anthropic/claude-opus-4-6");
  auto provider = reg->GetProviderForModel(ref);
  ASSERT_NE(provider, nullptr);
}

TEST(ProviderRegistryTest, NullForUnknownProvider) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  auto provider = reg->GetProvider("nonexistent");
  EXPECT_EQ(provider, nullptr);
}

TEST(ProviderRegistryTest, ProviderEntryInspection) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));

  ProviderEntry entry;
  entry.id = "ollama";
  entry.base_url = "http://localhost:11434/v1";
  entry.display_name = "Local Ollama";
  reg->AddProvider(entry);

  auto* e = reg->GetEntry("ollama");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->display_name, "Local Ollama");
  EXPECT_EQ(e->base_url, "http://localhost:11434/v1");
}

TEST(ProviderRegistryTest, LlamaCppProviderUsesMobileModelOptions) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  ProviderEntry entry;
  entry.id = "llama_cpp";
  entry.extra = {{"modelPath", "models/Qwen3.5-0.8B-Q4_K_M.gguf"},
                 {"mmprojPath", "models/mmproj.gguf"},
                 {"enableVulkan", false}};
  reg->AddProvider(entry);

  auto provider = reg->GetProvider("llama_cpp");
  ASSERT_NE(provider, nullptr);
  EXPECT_EQ(provider->GetProviderName(), "llama_cpp_mobile");

  auto* llama_provider =
      dynamic_cast<LlamaCppMobileProvider*>(provider.get());
  ASSERT_NE(llama_provider, nullptr);
  EXPECT_EQ(llama_provider->options().model_path,
            "models/Qwen3.5-0.8B-Q4_K_M.gguf");
  EXPECT_EQ(llama_provider->options().mmproj_path, "models/mmproj.gguf");
  EXPECT_FALSE(llama_provider->options().enable_vulkan);
}

TEST(ProviderRegistryTest, LlamaCppProviderResolvesRelativePathsAgainstModelsDir) {
  auto logger = make_logger("providers-llama-runtime");
  auto test_dir = std::filesystem::temp_directory_path() /
                  "ravbot_llama_cpp_mobile_provider";
  std::filesystem::remove_all(test_dir);
  std::filesystem::create_directories(test_dir);

  std::filesystem::path llm_path = test_dir / "Qwen3.5-0.8B-Q4_K_M.gguf";
  std::filesystem::path vlm_path =
      test_dir / "SmolVLM-500M-Instruct-Q8_0.gguf";
  std::filesystem::path mmproj_path =
      test_dir / "mmproj-SmolVLM-500M-Instruct-Q8_0.gguf";
  std::ofstream(llm_path.string()).put('\n');
  std::ofstream(vlm_path.string()).put('\n');
  std::ofstream(mmproj_path.string()).put('\n');

  LlamaCppMobileProvider::Options options;
  options.model_path = "Qwen3.5-0.8B-Q4_K_M.gguf";
  options.vision_model_path = "SmolVLM-500M-Instruct-Q8_0.gguf";
  options.mmproj_path = "mmproj-SmolVLM-500M-Instruct-Q8_0.gguf";
  options.models_dir = test_dir.string();
  options.enable_vulkan = true;

  LlamaCppMobileProvider provider(options, logger);

  const auto& status = provider.runtime_status();
  EXPECT_EQ(status.models_dir, test_dir.string());
  EXPECT_EQ(status.llm_model_path, llm_path.string());
  EXPECT_EQ(status.vision_model_path, vlm_path.string());
  EXPECT_EQ(status.mmproj_path, mmproj_path.string());
  EXPECT_TRUE(status.llm_model_exists);
  EXPECT_TRUE(status.vision_model_exists);
  EXPECT_TRUE(status.mmproj_exists);
  EXPECT_EQ(status.text_ready, status.backend_linked && status.llm_model_exists);
  EXPECT_EQ(status.vision_ready, status.backend_linked);
  EXPECT_EQ(status.vulkan_enabled,
            status.backend_linked && status.vulkan_requested);

  auto supported_models = provider.GetSupportedModels();
  ASSERT_EQ(supported_models.size(), 2u);
  EXPECT_EQ(supported_models[0], llm_path.string());
  EXPECT_EQ(supported_models[1], vlm_path.string());

  std::filesystem::remove_all(test_dir);
}

class TransportStubGoogleProvider : public ravbot::GoogleProvider {
 public:
  explicit TransportStubGoogleProvider(std::shared_ptr<spdlog::logger> logger)
      : GoogleProvider("test-key", "http://stub", 30, logger) {}

  mutable std::string api_response;
  mutable std::string requested_model;
  mutable std::string requested_payload;
  mutable bool requested_stream = false;

 protected:
  std::string MakeApiRequest(const std::string& model,
                             const std::string& json_payload,
                             bool stream) const override {
    requested_model = model;
    requested_payload = json_payload;
    requested_stream = stream;
    return api_response;
  }
};

class TransportStubQwenProvider : public ravbot::QwenProvider {
 public:
  explicit TransportStubQwenProvider(std::shared_ptr<spdlog::logger> logger)
      : QwenProvider("test-key", "http://stub", 30, logger) {}

  mutable std::string api_response;
  mutable bool requested_stream = false;

 protected:
  std::string MakeApiRequest(const std::string& json_payload,
                             bool stream) const override {
    (void)json_payload;
    requested_stream = stream;
    return api_response;
  }
};

TEST(GoogleProviderStreamTest, EmitsTerminalChunk) {
  TransportStubGoogleProvider provider(make_logger("google-stream"));
  provider.api_response =
      R"({"candidates":[{"content":{"parts":[{"text":"hello"}]}}]})";

  ravbot::ChatCompletionRequest request;
  request.model = "gemini-1.5-pro";
  request.messages.push_back({"user", "Hi"});

  std::string accumulated;
  bool saw_end = false;
  provider.ChatCompletionStream(
      request, [&](const ravbot::ChatCompletionResponse& chunk) {
        accumulated += chunk.content;
        if (chunk.is_stream_end) {
          saw_end = true;
        }
      });

  EXPECT_EQ(provider.requested_model, "gemini-1.5-pro");
  EXPECT_FALSE(provider.requested_stream);
  EXPECT_EQ(accumulated, "hello");
  EXPECT_TRUE(saw_end);
}

TEST(QwenProviderStreamTest, ParsesBufferedSseResponse) {
  TransportStubQwenProvider provider(make_logger("qwen-stream"));
  provider.api_response =
      "data: "
      "{\"choices\":[{\"delta\":{\"content\":\"Hel\"},\"finish_reason\":null}]}"
      "\n\n"
      "data: "
      "{\"choices\":[{\"delta\":{\"content\":\"lo\"},\"finish_reason\":null}]}"
      "\n\n"
      "data: "
      "{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],\"usage\":{"
      "\"prompt_tokens\":1,\"completion_tokens\":2,\"total_tokens\":3}}\n\n"
      "data: [DONE]\n\n";

  ravbot::ChatCompletionRequest request;
  request.model = "qwen-max";
  request.messages.push_back({"user", "Hi"});
  request.stream = true;

  std::string accumulated;
  ravbot::TokenUsage final_usage;
  bool saw_end = false;
  provider.ChatCompletionStream(
      request, [&](const ravbot::ChatCompletionResponse& chunk) {
        accumulated += chunk.content;
        if (chunk.is_stream_end) {
          final_usage = chunk.usage;
          saw_end = true;
        }
      });

  EXPECT_TRUE(provider.requested_stream);
  EXPECT_EQ(accumulated, "Hello");
  EXPECT_TRUE(saw_end);
  EXPECT_EQ(final_usage.prompt_tokens, 1);
  EXPECT_EQ(final_usage.completion_tokens, 2);
  EXPECT_EQ(final_usage.total_tokens, 3);
}

}  // namespace ravbot
