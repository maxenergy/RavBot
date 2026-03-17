// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "ravbot/plugins/plugin_manifest.hpp"
#include "test_helpers.hpp"

class PluginManifestTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = ravbot::test::MakeTestDir("ravbot_manifest_test");
    }

    void TearDown() override {
        if (std::filesystem::exists(test_dir_)) {
            std::filesystem::remove_all(test_dir_);
        }
    }

    std::filesystem::path test_dir_;
};

// --- Requirements: 22.1 - 清单解析测试 ---

TEST_F(PluginManifestTest, Parse_ValidManifest) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"description", "A test plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"}
    };

    auto manifest = ravbot::PluginManifest::Parse(j);
    EXPECT_EQ(manifest.id, "test-plugin");
    EXPECT_EQ(manifest.name, "Test Plugin");
    EXPECT_EQ(manifest.description, "A test plugin");
    EXPECT_EQ(manifest.version, "1.0.0");
    EXPECT_EQ(manifest.entry_point, "index.js");
}

TEST_F(PluginManifestTest, Parse_MissingId) {
    nlohmann::json j = {
        {"name", "Test Plugin"},
        {"version", "1.0.0"}
    };

    EXPECT_THROW(ravbot::PluginManifest::Parse(j), std::runtime_error);
}

TEST_F(PluginManifestTest, Parse_WithChannels) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"},
        {"channels", {"telegram", "slack"}}
    };

    auto manifest = ravbot::PluginManifest::Parse(j);
    ASSERT_EQ(manifest.channels.size(), 2u);
    EXPECT_EQ(manifest.channels[0], "telegram");
    EXPECT_EQ(manifest.channels[1], "slack");
}

TEST_F(PluginManifestTest, Parse_WithDependencies) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"},
        {"dependencies", {
            {{"pluginId", "dep1"}, {"versionRange", "^1.0.0"}},
            {{"pluginId", "dep2"}, {"versionRange", ">=2.0.0"}, {"optional", true}}
        }}
    };

    auto manifest = ravbot::PluginManifest::Parse(j);
    ASSERT_EQ(manifest.dependencies.size(), 2u);
    EXPECT_EQ(manifest.dependencies[0].plugin_id, "dep1");
    EXPECT_EQ(manifest.dependencies[0].version_range, "^1.0.0");
    EXPECT_FALSE(manifest.dependencies[0].optional);
    EXPECT_EQ(manifest.dependencies[1].plugin_id, "dep2");
    EXPECT_EQ(manifest.dependencies[1].version_range, ">=2.0.0");
    EXPECT_TRUE(manifest.dependencies[1].optional);
}

TEST_F(PluginManifestTest, Parse_WithExtraFields) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"},
        {"customField", "custom value"},
        {"anotherField", 123}
    };

    auto manifest = ravbot::PluginManifest::Parse(j);
    EXPECT_TRUE(manifest.extra_fields.contains("customField"));
    EXPECT_EQ(manifest.extra_fields["customField"], "custom value");
    EXPECT_TRUE(manifest.extra_fields.contains("anotherField"));
    EXPECT_EQ(manifest.extra_fields["anotherField"], 123);
}

// --- Requirements: 22.2, 22.5 - 清单验证测试 ---

TEST_F(PluginManifestTest, Validate_ValidManifest) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"}
    };

    auto errors = ravbot::PluginManifest::Validate(j);
    EXPECT_TRUE(errors.empty());
}

TEST_F(PluginManifestTest, Validate_MissingId) {
    nlohmann::json j = {
        {"name", "Test Plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"}
    };

    auto errors = ravbot::PluginManifest::Validate(j);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "Missing required field: id");
}

TEST_F(PluginManifestTest, Validate_MissingName) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"}
    };

    auto errors = ravbot::PluginManifest::Validate(j);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "Missing required field: name");
}

TEST_F(PluginManifestTest, Validate_MissingVersion) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"entryPoint", "index.js"}
    };

    auto errors = ravbot::PluginManifest::Validate(j);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "Missing required field: version");
}

TEST_F(PluginManifestTest, Validate_MissingEntryPoint) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"version", "1.0.0"}
    };

    auto errors = ravbot::PluginManifest::Validate(j);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "Missing required field: entryPoint");
}

TEST_F(PluginManifestTest, Validate_InvalidIdFormat) {
    nlohmann::json j = {
        {"id", "test plugin!"},  // 包含空格和特殊字符
        {"name", "Test Plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"}
    };

    auto errors = ravbot::PluginManifest::Validate(j);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(errors[0].find("must contain only letters") != std::string::npos);
}

TEST_F(PluginManifestTest, Validate_InvalidVersionFormat) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"version", "1.0"},  // 不是有效的 semver
        {"entryPoint", "index.js"}
    };

    auto errors = ravbot::PluginManifest::Validate(j);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(errors[0].find("must be a valid semver") != std::string::npos);
}

TEST_F(PluginManifestTest, Validate_UnsupportedSchemaVersion) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"},
        {"schemaVersion", "2.0"}  // 不支持的版本
    };

    auto errors = ravbot::PluginManifest::Validate(j);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(errors[0].find("Unsupported schemaVersion") != std::string::npos);
}

TEST_F(PluginManifestTest, Validate_InvalidDependencyFormat) {
    nlohmann::json j = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"},
        {"dependencies", {
            {{"pluginId", "dep1"}},  // 缺少 versionRange 是允许的
            {{"versionRange", "^1.0.0"}}  // 缺少 pluginId
        }}
    };

    auto errors = ravbot::PluginManifest::Validate(j);
    ASSERT_GE(errors.size(), 1u);
    EXPECT_TRUE(errors[0].find("missing required field: pluginId") != std::string::npos);
}

TEST_F(PluginManifestTest, Validate_MultipleErrors) {
    nlohmann::json j = {
        {"id", "test plugin!"},
        {"version", "1.0"},
        {"entryPoint", ""}
    };

    auto errors = ravbot::PluginManifest::Validate(j);
    EXPECT_GE(errors.size(), 3u);
}

// --- Requirements: 22.6 - 依赖验证测试 ---

TEST_F(PluginManifestTest, ValidateDependencies_Valid) {
    std::vector<ravbot::PluginDependency> deps = {
        {"dep1", "^1.0.0", false},
        {"dep2", ">=2.0.0", true},
        {"dep3", "~1.2.3", false}
    };

    auto errors = ravbot::PluginManifest::ValidateDependencies(deps);
    EXPECT_TRUE(errors.empty());
}

TEST_F(PluginManifestTest, ValidateDependencies_EmptyPluginId) {
    std::vector<ravbot::PluginDependency> deps = {
        {"", "^1.0.0", false}
    };

    auto errors = ravbot::PluginManifest::ValidateDependencies(deps);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(errors[0].find("plugin_id cannot be empty") != std::string::npos);
}

TEST_F(PluginManifestTest, ValidateDependencies_InvalidVersionRange) {
    std::vector<ravbot::PluginDependency> deps = {
        {"dep1", "invalid-version", false}
    };

    auto errors = ravbot::PluginManifest::ValidateDependencies(deps);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(errors[0].find("invalid version_range format") != std::string::npos);
}

// --- Requirements: 22.3, 22.7 - 清单模板生成测试 ---

TEST_F(PluginManifestTest, GenerateTemplate_Basic) {
    auto j = ravbot::PluginManifest::GenerateTemplate("my-plugin", "My Plugin");

    EXPECT_EQ(j["id"], "my-plugin");
    EXPECT_EQ(j["name"], "My Plugin");
    EXPECT_EQ(j["description"], "A RavBot plugin");
    EXPECT_EQ(j["version"], "1.0.0");
    EXPECT_EQ(j["schemaVersion"], "1.0");
    EXPECT_EQ(j["entryPoint"], "index.js");
    EXPECT_TRUE(j["channels"].is_array());
    EXPECT_TRUE(j["providers"].is_array());
    EXPECT_TRUE(j["skills"].is_array());
    EXPECT_TRUE(j["dependencies"].is_array());
    EXPECT_TRUE(j["configSchema"].is_object());
    EXPECT_TRUE(j["uiHints"].is_object());
}

// --- Requirements: 22.4 - Round-Trip 测试 ---

TEST_F(PluginManifestTest, RoundTrip_BasicManifest) {
    nlohmann::json original = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"description", "A test plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"},
        {"channels", {"telegram"}},
        {"providers", {"openai"}},
        {"skills", {"weather"}}
    };

    auto manifest = ravbot::PluginManifest::Parse(original);
    auto serialized = manifest.ToJson();

    EXPECT_EQ(serialized["id"], original["id"]);
    EXPECT_EQ(serialized["name"], original["name"]);
    EXPECT_EQ(serialized["description"], original["description"]);
    EXPECT_EQ(serialized["version"], original["version"]);
    EXPECT_EQ(serialized["entryPoint"], original["entryPoint"]);
    EXPECT_EQ(serialized["channels"], original["channels"]);
    EXPECT_EQ(serialized["providers"], original["providers"]);
    EXPECT_EQ(serialized["skills"], original["skills"]);
}

TEST_F(PluginManifestTest, RoundTrip_WithDependencies) {
    nlohmann::json original = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"},
        {"dependencies", {
            {{"pluginId", "dep1"}, {"versionRange", "^1.0.0"}},
            {{"pluginId", "dep2"}, {"versionRange", ">=2.0.0"}, {"optional", true}}
        }}
    };

    auto manifest = ravbot::PluginManifest::Parse(original);
    auto serialized = manifest.ToJson();

    ASSERT_TRUE(serialized.contains("dependencies"));
    ASSERT_EQ(serialized["dependencies"].size(), 2u);
    EXPECT_EQ(serialized["dependencies"][0]["pluginId"], "dep1");
    EXPECT_EQ(serialized["dependencies"][0]["versionRange"], "^1.0.0");
    EXPECT_EQ(serialized["dependencies"][1]["pluginId"], "dep2");
    EXPECT_EQ(serialized["dependencies"][1]["versionRange"], ">=2.0.0");
    EXPECT_TRUE(serialized["dependencies"][1]["optional"].get<bool>());
}

TEST_F(PluginManifestTest, RoundTrip_WithExtraFields) {
    nlohmann::json original = {
        {"id", "test-plugin"},
        {"name", "Test Plugin"},
        {"version", "1.0.0"},
        {"entryPoint", "index.js"},
        {"customField", "custom value"},
        {"anotherField", 123}
    };

    auto manifest = ravbot::PluginManifest::Parse(original);
    auto serialized = manifest.ToJson();

    EXPECT_TRUE(serialized.contains("customField"));
    EXPECT_EQ(serialized["customField"], "custom value");
    EXPECT_TRUE(serialized.contains("anotherField"));
    EXPECT_EQ(serialized["anotherField"], 123);
}

// --- Requirements: 22.1 - 文件加载测试 ---

TEST_F(PluginManifestTest, LoadFromFile_Valid) {
    auto manifest_path = test_dir_ / "manifest.json";
    std::ofstream f(manifest_path);
    f << R"({
        "id": "test-plugin",
        "name": "Test Plugin",
        "version": "1.0.0",
        "entryPoint": "index.js"
    })";
    f.close();

    auto manifest = ravbot::PluginManifest::LoadFromFile(manifest_path);
    EXPECT_EQ(manifest.id, "test-plugin");
    EXPECT_EQ(manifest.name, "Test Plugin");
    EXPECT_EQ(manifest.version, "1.0.0");
}

TEST_F(PluginManifestTest, LoadFromFile_InvalidJson) {
    auto manifest_path = test_dir_ / "invalid.json";
    std::ofstream f(manifest_path);
    f << "{ invalid json }";
    f.close();

    EXPECT_THROW(ravbot::PluginManifest::LoadFromFile(manifest_path), std::runtime_error);
}

TEST_F(PluginManifestTest, LoadFromFile_NonexistentFile) {
    auto manifest_path = test_dir_ / "nonexistent.json";
    EXPECT_THROW(ravbot::PluginManifest::LoadFromFile(manifest_path), std::runtime_error);
}

// --- PluginDependency 测试 ---

TEST_F(PluginManifestTest, PluginDependency_FromJson) {
    nlohmann::json j = {
        {"pluginId", "dep1"},
        {"versionRange", "^1.0.0"},
        {"optional", true}
    };

    auto dep = ravbot::PluginDependency::FromJson(j);
    EXPECT_EQ(dep.plugin_id, "dep1");
    EXPECT_EQ(dep.version_range, "^1.0.0");
    EXPECT_TRUE(dep.optional);
}

TEST_F(PluginManifestTest, PluginDependency_ToJson) {
    ravbot::PluginDependency dep;
    dep.plugin_id = "dep1";
    dep.version_range = "^1.0.0";
    dep.optional = true;

    auto j = dep.ToJson();
    EXPECT_EQ(j["pluginId"], "dep1");
    EXPECT_EQ(j["versionRange"], "^1.0.0");
    EXPECT_TRUE(j["optional"].get<bool>());
}

// --- PluginConfigUiHint 测试 ---

TEST_F(PluginManifestTest, PluginConfigUiHint_FromJson) {
    nlohmann::json j = {
        {"label", "API Key"},
        {"help", "Enter your API key"},
        {"tags", {"required", "secret"}},
        {"advanced", false},
        {"sensitive", true},
        {"placeholder", "sk-..."}
    };

    auto hint = ravbot::PluginConfigUiHint::FromJson(j);
    EXPECT_EQ(hint.label, "API Key");
    EXPECT_EQ(hint.help, "Enter your API key");
    ASSERT_EQ(hint.tags.size(), 2u);
    EXPECT_EQ(hint.tags[0], "required");
    EXPECT_EQ(hint.tags[1], "secret");
    EXPECT_FALSE(hint.advanced);
    EXPECT_TRUE(hint.sensitive);
    EXPECT_EQ(hint.placeholder, "sk-...");
}

TEST_F(PluginManifestTest, PluginConfigUiHint_ToJson) {
    ravbot::PluginConfigUiHint hint;
    hint.label = "API Key";
    hint.help = "Enter your API key";
    hint.tags = {"required", "secret"};
    hint.advanced = false;
    hint.sensitive = true;
    hint.placeholder = "sk-...";

    auto j = hint.ToJson();
    EXPECT_EQ(j["label"], "API Key");
    EXPECT_EQ(j["help"], "Enter your API key");
    ASSERT_EQ(j["tags"].size(), 2u);
    EXPECT_TRUE(j["sensitive"].get<bool>());
    EXPECT_EQ(j["placeholder"], "sk-...");
}
