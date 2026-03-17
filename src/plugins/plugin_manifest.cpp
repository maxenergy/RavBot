// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/plugins/plugin_manifest.hpp"
#include <fstream>
#include <stdexcept>
#include <regex>
#include <set>

namespace ravbot {

// ---------------------------------------------------------------------------
// Requirements: 22.6 - PluginDependency 实现
// ---------------------------------------------------------------------------

PluginDependency PluginDependency::FromJson(const nlohmann::json& j) {
  PluginDependency dep;
  dep.plugin_id = j.value("plugin_id", j.value("pluginId", ""));
  dep.version_range = j.value("version_range", j.value("versionRange", j.value("version", "")));
  dep.optional = j.value("optional", false);
  return dep;
}

nlohmann::json PluginDependency::ToJson() const {
  nlohmann::json j;
  j["pluginId"] = plugin_id;
  if (!version_range.empty()) {
    j["versionRange"] = version_range;
  }
  if (optional) {
    j["optional"] = optional;
  }
  return j;
}

// ---------------------------------------------------------------------------
// PluginConfigUiHint 实现
// ---------------------------------------------------------------------------

PluginConfigUiHint PluginConfigUiHint::FromJson(const nlohmann::json& j) {
  PluginConfigUiHint hint;
  hint.label = j.value("label", "");
  hint.help = j.value("help", "");
  hint.advanced = j.value("advanced", false);
  hint.sensitive = j.value("sensitive", false);
  hint.placeholder = j.value("placeholder", "");
  if (j.contains("tags") && j["tags"].is_array()) {
    for (const auto& t : j["tags"]) {
      if (t.is_string()) hint.tags.push_back(t.get<std::string>());
    }
  }
  return hint;
}

nlohmann::json PluginConfigUiHint::ToJson() const {
  nlohmann::json j;
  if (!label.empty()) j["label"] = label;
  if (!help.empty()) j["help"] = help;
  if (!tags.empty()) j["tags"] = tags;
  if (advanced) j["advanced"] = advanced;
  if (sensitive) j["sensitive"] = sensitive;
  if (!placeholder.empty()) j["placeholder"] = placeholder;
  return j;
}

// ---------------------------------------------------------------------------
// PluginManifest 实现
// ---------------------------------------------------------------------------

PluginManifest PluginManifest::Parse(const nlohmann::json& j) {
  PluginManifest m;

  if (!j.contains("id") || !j["id"].is_string()) {
    throw std::runtime_error("Plugin manifest missing required field: id");
  }
  m.id = j["id"].get<std::string>();

  m.name = j.value("name", m.id);
  m.description = j.value("description", "");
  m.version = j.value("version", "");
  m.schema_version = j.value("schema_version", j.value("schemaVersion", "1.0"));
  m.entry_point = j.value("entry_point", j.value("entryPoint", ""));
  m.kind = j.value("kind", "");

  if (j.contains("channels") && j["channels"].is_array()) {
    for (const auto& ch : j["channels"]) {
      if (ch.is_string()) m.channels.push_back(ch.get<std::string>());
    }
  }
  if (j.contains("providers") && j["providers"].is_array()) {
    for (const auto& p : j["providers"]) {
      if (p.is_string()) m.providers.push_back(p.get<std::string>());
    }
  }
  if (j.contains("skills") && j["skills"].is_array()) {
    for (const auto& s : j["skills"]) {
      if (s.is_string()) m.skills.push_back(s.get<std::string>());
    }
  }

  // Requirements: 22.6 - 解析依赖项
  if (j.contains("dependencies") && j["dependencies"].is_array()) {
    for (const auto& dep_json : j["dependencies"]) {
      m.dependencies.push_back(PluginDependency::FromJson(dep_json));
    }
  }

  if (j.contains("configSchema")) {
    m.config_schema = j["configSchema"];
  } else {
    m.config_schema = nlohmann::json::object();
  }

  if (j.contains("uiHints") && j["uiHints"].is_object()) {
    for (auto it = j["uiHints"].begin(); it != j["uiHints"].end(); ++it) {
      m.ui_hints[it.key()] = PluginConfigUiHint::FromJson(it.value());
    }
  }

  // Requirements: 22.8 - 保留扩展字段
  // 保存所有未被标准字段处理的字段
  m.extra_fields = nlohmann::json::object();
  std::set<std::string> known_fields = {
    "id", "name", "description", "version", "schema_version", "schemaVersion",
    "entry_point", "entryPoint", "kind", "channels", "providers", "skills",
    "dependencies", "configSchema", "uiHints"
  };
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (known_fields.find(it.key()) == known_fields.end()) {
      m.extra_fields[it.key()] = it.value();
    }
  }

  return m;
}

PluginManifest PluginManifest::LoadFromFile(const std::filesystem::path& path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    throw std::runtime_error("Cannot open plugin manifest: " + path.string());
  }
  nlohmann::json j;
  try {
    ifs >> j;
  } catch (const nlohmann::json::parse_error& e) {
    throw std::runtime_error("Invalid JSON in " + path.string() + ": " + e.what());
  }
  return Parse(j);
}

nlohmann::json PluginManifest::ToJson() const {
  nlohmann::json j;
  j["id"] = id;
  if (!name.empty() && name != id) j["name"] = name;
  if (!description.empty()) j["description"] = description;
  if (!version.empty()) j["version"] = version;
  if (!schema_version.empty() && schema_version != "1.0") {
    j["schemaVersion"] = schema_version;
  }
  if (!entry_point.empty()) j["entryPoint"] = entry_point;
  if (!kind.empty()) j["kind"] = kind;
  if (!channels.empty()) j["channels"] = channels;
  if (!providers.empty()) j["providers"] = providers;
  if (!skills.empty()) j["skills"] = skills;

  // Requirements: 22.6 - 序列化依赖项
  if (!dependencies.empty()) {
    nlohmann::json deps_array = nlohmann::json::array();
    for (const auto& dep : dependencies) {
      deps_array.push_back(dep.ToJson());
    }
    j["dependencies"] = deps_array;
  }

  if (!config_schema.is_null() && !config_schema.empty()) {
    j["configSchema"] = config_schema;
  }

  // 序列化 UI hints
  if (!ui_hints.empty()) {
    nlohmann::json hints_obj = nlohmann::json::object();
    for (const auto& [key, hint] : ui_hints) {
      hints_obj[key] = hint.ToJson();
    }
    j["uiHints"] = hints_obj;
  }

  // Requirements: 22.8 - 恢复扩展字段
  for (auto it = extra_fields.begin(); it != extra_fields.end(); ++it) {
    j[it.key()] = it.value();
  }

  return j;
}

// ---------------------------------------------------------------------------
// Requirements: 22.2, 22.5 - 清单验证
// ---------------------------------------------------------------------------

std::vector<std::string> PluginManifest::Validate(const nlohmann::json& j) {
  std::vector<std::string> errors;

  // 验证根对象类型
  if (!j.is_object()) {
    errors.push_back("Manifest must be a JSON object");
    return errors;
  }

  // 验证必填字段: id
  if (!j.contains("id")) {
    errors.push_back("Missing required field: id");
  } else if (!j["id"].is_string()) {
    errors.push_back("Field 'id' must be a string");
  } else if (j["id"].get<std::string>().empty()) {
    errors.push_back("Field 'id' cannot be empty");
  } else {
    // 验证 id 格式 (字母、数字、下划线、连字符)
    std::string id = j["id"].get<std::string>();
    std::regex id_regex("^[a-zA-Z0-9_-]+$");
    if (!std::regex_match(id, id_regex)) {
      errors.push_back("Field 'id' must contain only letters, numbers, underscores, and hyphens");
    }
  }

  // 验证必填字段: name
  if (!j.contains("name")) {
    errors.push_back("Missing required field: name");
  } else if (!j["name"].is_string()) {
    errors.push_back("Field 'name' must be a string");
  } else if (j["name"].get<std::string>().empty()) {
    errors.push_back("Field 'name' cannot be empty");
  }

  // 验证必填字段: version
  if (!j.contains("version")) {
    errors.push_back("Missing required field: version");
  } else if (!j["version"].is_string()) {
    errors.push_back("Field 'version' must be a string");
  } else {
    // 验证 semver 格式
    std::string version = j["version"].get<std::string>();
    std::regex semver_regex(R"(^\d+\.\d+\.\d+(-[a-zA-Z0-9.-]+)?(\+[a-zA-Z0-9.-]+)?$)");
    if (!std::regex_match(version, semver_regex)) {
      errors.push_back("Field 'version' must be a valid semver (e.g., '1.0.0')");
    }
  }

  // 验证必填字段: entry_point
  if (!j.contains("entry_point") && !j.contains("entryPoint")) {
    errors.push_back("Missing required field: entryPoint");
  } else {
    const auto& ep = j.contains("entryPoint") ? j["entryPoint"] : j["entry_point"];
    if (!ep.is_string()) {
      errors.push_back("Field 'entryPoint' must be a string");
    } else if (ep.get<std::string>().empty()) {
      errors.push_back("Field 'entryPoint' cannot be empty");
    }
  }

  // Requirements: 22.5 - 验证 schema 版本兼容性
  if (j.contains("schemaVersion") || j.contains("schema_version")) {
    const auto& sv = j.contains("schemaVersion") ? j["schemaVersion"] : j["schema_version"];
    if (!sv.is_string()) {
      errors.push_back("Field 'schemaVersion' must be a string");
    } else {
      std::string schema_ver = sv.get<std::string>();
      // 当前支持 1.0 和 1.1
      if (schema_ver != "1.0" && schema_ver != "1.1") {
        errors.push_back("Unsupported schemaVersion: " + schema_ver + " (supported: 1.0, 1.1)");
      }
    }
  }

  // 验证可选字段类型
  if (j.contains("description") && !j["description"].is_string()) {
    errors.push_back("Field 'description' must be a string");
  }
  if (j.contains("kind") && !j["kind"].is_string()) {
    errors.push_back("Field 'kind' must be a string");
  }

  // 验证数组字段
  if (j.contains("channels") && !j["channels"].is_array()) {
    errors.push_back("Field 'channels' must be an array");
  }
  if (j.contains("providers") && !j["providers"].is_array()) {
    errors.push_back("Field 'providers' must be an array");
  }
  if (j.contains("skills") && !j["skills"].is_array()) {
    errors.push_back("Field 'skills' must be an array");
  }

  // Requirements: 22.6 - 验证依赖项
  if (j.contains("dependencies")) {
    if (!j["dependencies"].is_array()) {
      errors.push_back("Field 'dependencies' must be an array");
    } else {
      for (size_t i = 0; i < j["dependencies"].size(); ++i) {
        const auto& dep = j["dependencies"][i];
        std::string prefix = "dependencies[" + std::to_string(i) + "]";

        if (!dep.is_object()) {
          errors.push_back(prefix + " must be an object");
          continue;
        }

        if (!dep.contains("pluginId") && !dep.contains("plugin_id")) {
          errors.push_back(prefix + " missing required field: pluginId");
        } else {
          const auto& pid = dep.contains("pluginId") ? dep["pluginId"] : dep["plugin_id"];
          if (!pid.is_string() || pid.get<std::string>().empty()) {
            errors.push_back(prefix + ".pluginId must be a non-empty string");
          }
        }

        if (dep.contains("versionRange") && !dep["versionRange"].is_string()) {
          errors.push_back(prefix + ".versionRange must be a string");
        }
        if (dep.contains("version_range") && !dep["version_range"].is_string()) {
          errors.push_back(prefix + ".version_range must be a string");
        }
        if (dep.contains("optional") && !dep["optional"].is_boolean()) {
          errors.push_back(prefix + ".optional must be a boolean");
        }
      }
    }
  }

  // 验证 configSchema
  if (j.contains("configSchema") && !j["configSchema"].is_object()) {
    errors.push_back("Field 'configSchema' must be an object");
  }

  // 验证 uiHints
  if (j.contains("uiHints") && !j["uiHints"].is_object()) {
    errors.push_back("Field 'uiHints' must be an object");
  }

  return errors;
}

// ---------------------------------------------------------------------------
// Requirements: 22.6 - 依赖验证
// ---------------------------------------------------------------------------

std::vector<std::string> PluginManifest::ValidateDependencies(
    const std::vector<PluginDependency>& deps) {
  std::vector<std::string> errors;

  for (size_t i = 0; i < deps.size(); ++i) {
    const auto& dep = deps[i];
    std::string prefix = "Dependency[" + std::to_string(i) + "]";

    if (dep.plugin_id.empty()) {
      errors.push_back(prefix + " plugin_id cannot be empty");
    }

    // 验证版本范围格式 (简单验证)
    if (!dep.version_range.empty()) {
      // 支持的格式: ^1.0.0, >=1.0.0, ~1.0.0, 1.0.0, *
      std::regex version_range_regex(
          R"(^(\^|~|>=|<=|>|<)?\d+\.\d+\.\d+(-[a-zA-Z0-9.-]+)?(\+[a-zA-Z0-9.-]+)?$|^\*$)");
      if (!std::regex_match(dep.version_range, version_range_regex)) {
        errors.push_back(prefix + " invalid version_range format: " + dep.version_range);
      }
    }
  }

  return errors;
}

// ---------------------------------------------------------------------------
// Requirements: 22.3, 22.7 - 生成清单模板
// ---------------------------------------------------------------------------

nlohmann::json PluginManifest::GenerateTemplate(const std::string& plugin_id,
                                                  const std::string& plugin_name) {
  nlohmann::json j;
  j["id"] = plugin_id;
  j["name"] = plugin_name;
  j["description"] = "A RavBot plugin";
  j["version"] = "1.0.0";
  j["schemaVersion"] = "1.0";
  j["entryPoint"] = "index.js";
  j["channels"] = nlohmann::json::array();
  j["providers"] = nlohmann::json::array();
  j["skills"] = nlohmann::json::array();
  j["dependencies"] = nlohmann::json::array();
  j["configSchema"] = nlohmann::json::object();
  j["uiHints"] = nlohmann::json::object();
  return j;
}

std::string plugin_origin_to_string(PluginOrigin origin) {
  switch (origin) {
    case PluginOrigin::kBundled:   return "bundled";
    case PluginOrigin::kGlobal:    return "global";
    case PluginOrigin::kWorkspace: return "workspace";
    case PluginOrigin::kConfig:    return "config";
  }
  return "unknown";
}

}  // namespace ravbot
