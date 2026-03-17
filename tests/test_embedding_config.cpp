// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/core/embedding_config.hpp"

#include <gtest/gtest.h>
#include <fstream>
#include <cstdlib>

using namespace ravbot;

TEST(EmbeddingConfigTest, ExpandEnvVars) {
  // Set test environment variable
  setenv("TEST_VAR", "test_value", 1);

  std::string input = "prefix_${TEST_VAR}_suffix";
  std::string output = EmbeddingConfig::ExpandEnvVars(input);

  EXPECT_EQ(output, "prefix_test_value_suffix");

  unsetenv("TEST_VAR");
}

TEST(EmbeddingConfigTest, ExpandMultipleEnvVars) {
  setenv("VAR1", "value1", 1);
  setenv("VAR2", "value2", 1);

  std::string input = "${VAR1}_${VAR2}";
  std::string output = EmbeddingConfig::ExpandEnvVars(input);

  EXPECT_EQ(output, "value1_value2");

  unsetenv("VAR1");
  unsetenv("VAR2");
}

TEST(EmbeddingConfigTest, GetEnvVar) {
  setenv("TEST_VAR", "test_value", 1);

  std::string value = EmbeddingConfig::GetEnvVar("TEST_VAR");
  EXPECT_EQ(value, "test_value");

  std::string default_value = EmbeddingConfig::GetEnvVar("NONEXISTENT", "default");
  EXPECT_EQ(default_value, "default");

  unsetenv("TEST_VAR");
}

TEST(EmbeddingConfigTest, LoadFromJsonMock) {
  nlohmann::json config = {
    {"embedding", {
      {"default_provider", "mock"},
      {"providers", {
        {"mock", {
          {"type", "mock"},
          {"dimension", 256}
        }}
      }}
    }}
  };

  auto registry = EmbeddingConfig::LoadFromJson(config);

  ASSERT_NE(registry, nullptr);
  EXPECT_TRUE(registry->HasProvider("mock"));

  auto provider = registry->GetProvider("mock");
  ASSERT_NE(provider, nullptr);
  EXPECT_EQ(provider->GetDimension(), 256);
  EXPECT_EQ(provider->GetName(), "mock");
}

TEST(EmbeddingConfigTest, LoadFromJsonLocal) {
  nlohmann::json config = {
    {"embedding", {
      {"default_provider", "local"},
      {"providers", {
        {"local", {
          {"type", "local"},
          {"dimension", 384}
        }}
      }}
    }}
  };

  auto registry = EmbeddingConfig::LoadFromJson(config);

  ASSERT_NE(registry, nullptr);
  EXPECT_TRUE(registry->HasProvider("local"));

  auto provider = registry->GetProvider("local");
  ASSERT_NE(provider, nullptr);
  EXPECT_EQ(provider->GetDimension(), 384);
  EXPECT_EQ(provider->GetName(), "local");
}

TEST(EmbeddingConfigTest, LoadFromJsonOpenAI) {
  setenv("TEST_OPENAI_KEY", "test_key", 1);

  nlohmann::json config = {
    {"embedding", {
      {"providers", {
        {"openai", {
          {"type", "openai"},
          {"api_key", "${TEST_OPENAI_KEY}"},
          {"model", "text-embedding-3-small"},
          {"batch_size", 50}
        }}
      }}
    }}
  };

  auto registry = EmbeddingConfig::LoadFromJson(config);

  ASSERT_NE(registry, nullptr);
  EXPECT_TRUE(registry->HasProvider("openai"));

  auto provider = registry->GetProvider("openai");
  ASSERT_NE(provider, nullptr);
  EXPECT_EQ(provider->GetName(), "openai");
  EXPECT_EQ(provider->GetModel(), "text-embedding-3-small");

  unsetenv("TEST_OPENAI_KEY");
}

TEST(EmbeddingConfigTest, LoadFromJsonAnthropic) {
  setenv("TEST_VOYAGE_KEY", "test_key", 1);

  nlohmann::json config = {
    {"embedding", {
      {"providers", {
        {"anthropic", {
          {"type", "anthropic"},
          {"api_key", "${TEST_VOYAGE_KEY}"},
          {"model", "voyage-3"},
          {"batch_size", 64},
          {"input_type", "document"}
        }}
      }}
    }}
  };

  auto registry = EmbeddingConfig::LoadFromJson(config);

  ASSERT_NE(registry, nullptr);
  EXPECT_TRUE(registry->HasProvider("anthropic"));

  auto provider = registry->GetProvider("anthropic");
  ASSERT_NE(provider, nullptr);
  EXPECT_EQ(provider->GetName(), "anthropic");
  EXPECT_EQ(provider->GetModel(), "voyage-3");

  unsetenv("TEST_VOYAGE_KEY");
}

TEST(EmbeddingConfigTest, LoadMultipleProviders) {
  nlohmann::json config = {
    {"embedding", {
      {"default_provider", "local"},
      {"providers", {
        {"mock", {
          {"type", "mock"},
          {"dimension", 128}
        }},
        {"local", {
          {"type", "local"},
          {"dimension", 256}
        }}
      }}
    }}
  };

  auto registry = EmbeddingConfig::LoadFromJson(config);

  ASSERT_NE(registry, nullptr);
  EXPECT_TRUE(registry->HasProvider("mock"));
  EXPECT_TRUE(registry->HasProvider("local"));

  auto default_provider = registry->GetDefaultProvider();
  ASSERT_NE(default_provider, nullptr);
  EXPECT_EQ(default_provider->GetName(), "local");
}

TEST(EmbeddingConfigTest, LoadFromFile) {
  // Create temporary config file
  std::string config_path = "/tmp/test_embedding_config.json";
  nlohmann::json config = {
    {"embedding", {
      {"default_provider", "mock"},
      {"providers", {
        {"mock", {
          {"type", "mock"},
          {"dimension", 128}
        }}
      }}
    }}
  };

  std::ofstream file(config_path);
  file << config.dump(2);
  file.close();

  auto registry = EmbeddingConfig::LoadFromFile(config_path);

  ASSERT_NE(registry, nullptr);
  EXPECT_TRUE(registry->HasProvider("mock"));

  // Clean up
  std::remove(config_path.c_str());
}

TEST(EmbeddingConfigTest, LoadFromNonexistentFile) {
  auto registry = EmbeddingConfig::LoadFromFile("/nonexistent/path/config.json");
  EXPECT_EQ(registry, nullptr);
}

TEST(EmbeddingConfigTest, EmptyConfig) {
  nlohmann::json config = {};

  auto registry = EmbeddingConfig::LoadFromJson(config);

  ASSERT_NE(registry, nullptr);
  EXPECT_EQ(registry->ListProviders().size(), 0);
}

TEST(EmbeddingConfigTest, UnknownProviderType) {
  nlohmann::json config = {
    {"embedding", {
      {"providers", {
        {"unknown", {
          {"type", "unknown_type"}
        }}
      }}
    }}
  };

  auto registry = EmbeddingConfig::LoadFromJson(config);

  ASSERT_NE(registry, nullptr);
  EXPECT_FALSE(registry->HasProvider("unknown"));
}

TEST(EmbeddingConfigTest, CreateProviderMock) {
  nlohmann::json config = {
    {"type", "mock"},
    {"dimension", 512}
  };

  auto provider = EmbeddingConfig::CreateProvider("mock", config);

  ASSERT_NE(provider, nullptr);
  EXPECT_EQ(provider->GetDimension(), 512);
}

TEST(EmbeddingConfigTest, CreateProviderLocal) {
  nlohmann::json config = {
    {"type", "local"},
    {"dimension", 256}
  };

  auto provider = EmbeddingConfig::CreateProvider("local", config);

  ASSERT_NE(provider, nullptr);
  EXPECT_EQ(provider->GetDimension(), 256);
}
