// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>

namespace ravbot {

// Requirements: 22.6 - 依赖项定义
struct PluginDependency {
  std::string plugin_id;      // 依赖的插件 ID
  std::string version_range;  // 版本范围 (e.g., "^1.0.0", ">=2.0.0")
  bool optional = false;      // 是否为可选依赖

  static PluginDependency FromJson(const nlohmann::json& j);
  nlohmann::json ToJson() const;
};

struct PluginConfigUiHint {
  std::string label;
  std::string help;
  std::vector<std::string> tags;
  bool advanced = false;
  bool sensitive = false;
  std::string placeholder;

  static PluginConfigUiHint FromJson(const nlohmann::json& j);
  nlohmann::json ToJson() const;
};

struct PluginManifest {
  std::string id;
  std::string name;
  std::string description;
  std::string version;
  std::string schema_version = "1.0";  // Requirements: 22.5 - schema 版本
  std::string entry_point;             // Requirements: 22.2 - 入口点
  std::string kind;  // "memory" or empty
  std::vector<std::string> channels;
  std::vector<std::string> providers;
  std::vector<std::string> skills;
  std::vector<PluginDependency> dependencies;  // Requirements: 22.6
  nlohmann::json config_schema;
  std::map<std::string, PluginConfigUiHint> ui_hints;
  nlohmann::json extra_fields;  // Requirements: 22.8 - 扩展字段

  static PluginManifest Parse(const nlohmann::json& j);
  static PluginManifest LoadFromFile(const std::filesystem::path& path);
  nlohmann::json ToJson() const;

  // Requirements: 22.2, 22.5 - 清单验证
  // 验证清单的有效性,返回错误信息列表
  static std::vector<std::string> Validate(const nlohmann::json& j);

  // Requirements: 22.6 - 依赖验证
  // 验证依赖项格式的有效性
  static std::vector<std::string> ValidateDependencies(
      const std::vector<PluginDependency>& deps);

  // Requirements: 22.3, 22.7 - 生成清单模板
  // 生成一个基本的清单模板
  static nlohmann::json GenerateTemplate(const std::string& plugin_id,
                                          const std::string& plugin_name);
};

enum class PluginOrigin {
  kBundled,
  kGlobal,
  kWorkspace,
  kConfig,
};

std::string plugin_origin_to_string(PluginOrigin origin);

struct PluginCandidate {
  std::string id_hint;
  std::filesystem::path source;
  std::filesystem::path root_dir;
  PluginOrigin origin;
  std::string package_name;
  std::string package_version;
  std::string package_description;
  std::optional<PluginManifest> manifest;
};

}  // namespace ravbot
