// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/config.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <regex>
#include <cstdlib>
#include <spdlog/spdlog.h>

namespace ravbot {

// ---------------------------------------------------------------------------
// ${VAR} environment variable substitution
// ---------------------------------------------------------------------------

static std::string substitute_env_vars(const std::string& input) {
    static const std::regex env_re(R"(\$\{([^}]+)\})");
    std::string result;
    auto begin = std::sregex_iterator(input.begin(), input.end(), env_re);
    auto end = std::sregex_iterator();

    size_t last_pos = 0;
    for (auto it = begin; it != end; ++it) {
        auto& match = *it;
        result.append(input, last_pos, match.position() - last_pos);
        std::string var_name = match[1].str();
        const char* env_val = std::getenv(var_name.c_str());
        if (env_val) {
            result.append(env_val);
        }
        // If env var not set, replace with empty string (same as OpenClaw)
        last_pos = match.position() + match.length();
    }
    result.append(input, last_pos, std::string::npos);
    return result;
}

// ---------------------------------------------------------------------------
// JSON5 preprocessing: strip comments and trailing commas
// ---------------------------------------------------------------------------

static std::string strip_json5(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    size_t i = 0;
    const size_t len = input.size();

    while (i < len) {
        // --- Inside a string literal: copy verbatim, respecting escapes ---
        if (input[i] == '"') {
            out += '"';
            ++i;
            while (i < len && input[i] != '"') {
                if (input[i] == '\\' && i + 1 < len) {
                    out += input[i];
                    out += input[i + 1];
                    i += 2;
                } else {
                    out += input[i];
                    ++i;
                }
            }
            if (i < len) {
                out += '"';  // closing quote
                ++i;
            }
            continue;
        }

        // --- Line comment: // ---
        if (i + 1 < len && input[i] == '/' && input[i + 1] == '/') {
            i += 2;
            while (i < len && input[i] != '\n') ++i;
            continue;
        }

        // --- Block comment: /* ... */ ---
        if (i + 1 < len && input[i] == '/' && input[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(input[i] == '*' && input[i + 1] == '/')) ++i;
            if (i + 1 < len) i += 2;  // skip */
            continue;
        }

        out += input[i];
        ++i;
    }

    // --- Strip trailing commas before } or ] ---
    std::string result;
    result.reserve(out.size());
    for (size_t j = 0; j < out.size(); ++j) {
        if (out[j] == ',') {
            // Look ahead past whitespace for } or ]
            size_t k = j + 1;
            while (k < out.size() && (out[k] == ' ' || out[k] == '\t' ||
                                       out[k] == '\n' || out[k] == '\r')) {
                ++k;
            }
            if (k < out.size() && (out[k] == '}' || out[k] == ']')) {
                continue;  // skip trailing comma
            }
        }
        result += out[j];
    }

    return result;
}

static void expand_env_in_json(nlohmann::json& j) {
    if (j.is_string()) {
        auto& s = j.get_ref<std::string&>();
        if (s.find("${") != std::string::npos) {
            s = substitute_env_vars(s);
        }
    } else if (j.is_object()) {
        for (auto& [key, value] : j.items()) {
            expand_env_in_json(value);
        }
    } else if (j.is_array()) {
        for (auto& element : j) {
            expand_env_in_json(element);
        }
    }
}

AgentConfig AgentConfig::FromJson(const nlohmann::json& json) {
    AgentConfig config;
    // model can be a string or an object (primary/fallbacks) — only parse string here
    if (json.contains("model") && json["model"].is_string()) {
        config.model = json["model"].get<std::string>();
    }
    config.max_iterations = json.value("maxIterations", json.value("max_iterations", kDefaultMaxIterations));
    config.temperature = json.value("temperature", kDefaultTemperature);
    config.max_tokens = json.value("maxTokens", json.value("max_tokens", kDefaultMaxTokens));
    config.context_window = json.value("contextWindow",
                                       json.value("context_window", kDefaultContextWindow));
    config.thinking = json.value("thinking", "off");
    config.fallbacks = json.value("fallbacks", std::vector<std::string>{});

    // Compaction settings
    config.auto_compact = json.value("autoCompact", json.value("auto_compact", true));
    config.compact_max_messages = json.value("compactMaxMessages",
                                              json.value("compact_max_messages", kDefaultCompactMaxMessages));
    config.compact_keep_recent = json.value("compactKeepRecent",
                                             json.value("compact_keep_recent", kDefaultCompactKeepRecent));
    config.compact_max_tokens = json.value("compactMaxTokens",
                                            json.value("compact_max_tokens", kDefaultCompactMaxTokens));
    config.compact_timeout_seconds = json.value("compactTimeoutSeconds",
                                                 json.value("compact_timeout_seconds", 900));
    return config;
}

ModelCost ModelCost::FromJson(const nlohmann::json& json) {
    ModelCost c;
    c.input = json.value("input", 0.0);
    c.output = json.value("output", 0.0);
    c.cache_read = json.value("cacheRead", json.value("cache_read", 0.0));
    c.cache_write = json.value("cacheWrite", json.value("cache_write", 0.0));
    return c;
}

ModelDefinition ModelDefinition::FromJson(const nlohmann::json& json) {
    ModelDefinition m;
    m.id = json.value("id", "");
    m.name = json.value("name", "");
    m.reasoning = json.value("reasoning", false);
    m.input = json.value("input", std::vector<std::string>{"text"});
    if (json.contains("cost") && json["cost"].is_object()) {
        m.cost = ModelCost::FromJson(json["cost"]);
    }
    m.context_window = json.value("contextWindow", json.value("context_window", 0));
    m.max_tokens = json.value("maxTokens", json.value("max_tokens", 0));
    m.api_key = json.value("apiKey", json.value("api_key", ""));
    m.api_key_env = json.value("apiKeyEnv", json.value("api_key_env", ""));
    m.base_url = json.value("baseUrl", json.value("base_url", ""));
    m.api = json.value("api", "");
    m.timeout = json.value("timeout", 0);
    return m;
}

ModelEntryConfig ModelEntryConfig::FromJson(const nlohmann::json& json) {
    ModelEntryConfig c;
    c.alias = json.value("alias", "");
    if (json.contains("params") && json["params"].is_object()) {
        c.params = json["params"];
    }
    return c;
}

ProviderConfig ProviderConfig::FromJson(const nlohmann::json& json) {
    ProviderConfig config;
    config.api_key = json.value("apiKey", json.value("api_key", ""));
    config.base_url = json.value("baseUrl", json.value("base_url", ""));
    config.api = json.value("api", "");
    config.timeout = json.value("timeout", kDefaultProviderTimeoutSec);
    if (json.contains("models") && json["models"].is_array()) {
        for (const auto& m : json["models"]) {
            config.models.push_back(ModelDefinition::FromJson(m));
        }
    }
    return config;
}

ChannelConfig ChannelConfig::FromJson(const nlohmann::json& json) {
    ChannelConfig config;
    config.enabled = json.value("enabled", false);
    // Support both "token" and "botToken" fields
    if (json.contains("botToken")) {
        config.token = json["botToken"].get<std::string>();
    } else {
        config.token = json.value("token", "");
    }
    config.allowed_ids = json.value("allowed_ids", std::vector<std::string>{});
    // Store the full raw JSON so platform-specific fields are preserved
    config.raw = json;
    return config;
}

ToolConfig ToolConfig::FromJson(const nlohmann::json& json) {
    ToolConfig config;
    config.enabled = json.value("enabled", true);
    config.allowed_paths = json.value("allowed_paths", std::vector<std::string>{});
    config.denied_paths = json.value("denied_paths", std::vector<std::string>{});
    config.allowed_cmds = json.value("allowed_cmds", std::vector<std::string>{});
    config.denied_cmds = json.value("denied_cmds", std::vector<std::string>{});
    config.timeout = json.value("timeout", kDefaultToolTimeoutSec);
    return config;
}

ToolPermissionConfig ToolPermissionConfig::FromJson(const nlohmann::json& json) {
    ToolPermissionConfig config;
    std::string profile = json.value("profile", "");
    if (profile == "mobile_safe") {
        config.allow = {"group:mobile_safe"};
        config.deny = json.value("deny", std::vector<std::string>{});
        return config;
    }
    config.allow = json.value("allow", std::vector<std::string>{"group:fs", "group:runtime"});
    config.deny = json.value("deny", std::vector<std::string>{});
    return config;
}

MCPServerConfig MCPServerConfig::FromJson(const nlohmann::json& json) {
    MCPServerConfig config;
    config.name = json.value("name", "");
    config.url = json.value("url", "");
    config.timeout = json.value("timeout", kDefaultMcpTimeoutSec);
    return config;
}

MCPConfig MCPConfig::FromJson(const nlohmann::json& json) {
    MCPConfig config;
    if (json.contains("servers") && json["servers"].is_array()) {
        for (const auto& server_json : json["servers"]) {
            config.servers.push_back(MCPServerConfig::FromJson(server_json));
        }
    }
    return config;
}

MobileRuntimeConfig MobileRuntimeConfig::FromJson(const nlohmann::json& json) {
    MobileRuntimeConfig config;
    config.enabled = json.value("enabled", false);
    config.mode = json.value("mode", "local");
    config.foreground_only = json.value("foregroundOnly",
                                        json.value("foreground_only", true));
    config.continuous_vision = json.value("continuousVision",
                                          json.value("continuous_vision", true));
    return config;
}

MobileModelsConfig MobileModelsConfig::FromJson(const nlohmann::json& json) {
    MobileModelsConfig config;
    config.llm_model = json.value("llmModel", json.value("llm_model", config.llm_model));
    config.vlm_model = json.value("vlmModel", json.value("vlm_model", config.vlm_model));
    config.mmproj_model = json.value("mmprojModel",
                                     json.value("mmproj_model", config.mmproj_model));
    config.stt_model = json.value("sttModel", json.value("stt_model", config.stt_model));
    config.tts_voice = json.value("ttsVoice", json.value("tts_voice", config.tts_voice));
    config.use_vulkan = json.value("useVulkan", json.value("use_vulkan", true));
    return config;
}

MobileAudioConfig MobileAudioConfig::FromJson(const nlohmann::json& json) {
    MobileAudioConfig config;
    config.auto_listen = json.value("autoListen", json.value("auto_listen", true));
    config.tap_to_talk_enabled = json.value("tapToTalkEnabled",
                                            json.value("tap_to_talk_enabled", true));
    config.wake_word_enabled = json.value("wakeWordEnabled",
                                          json.value("wake_word_enabled", false));
    config.barge_in_enabled = json.value("bargeInEnabled",
                                         json.value("barge_in_enabled", true));
    config.sample_rate = json.value("sampleRate", json.value("sample_rate", 16000));
    return config;
}

MobileVisionConfig MobileVisionConfig::FromJson(const nlohmann::json& json) {
    MobileVisionConfig config;
    config.enabled = json.value("enabled", true);
    config.sample_fps = json.value("sampleFps", json.value("sample_fps", 0.5));
    config.foreground_only = json.value("foregroundOnly",
                                        json.value("foreground_only", true));
    config.persist_raw_frames = json.value("persistRawFrames",
                                           json.value("persist_raw_frames", false));
    config.scene_change_threshold = json.value(
        "sceneChangeThreshold",
        json.value("scene_change_threshold", 0.2));
    return config;
}

MobileAvatarConfig MobileAvatarConfig::FromJson(const nlohmann::json& json) {
    MobileAvatarConfig config;
    config.renderer = json.value("renderer", "face2d");
    config.default_state = json.value("defaultState",
                                      json.value("default_state", "idle"));
    config.lip_sync_enabled = json.value("lipSyncEnabled",
                                         json.value("lip_sync_enabled", true));
    return config;
}

MobileConfig MobileConfig::FromJson(const nlohmann::json& json) {
    MobileConfig config;
    if (json.contains("runtime") && json["runtime"].is_object()) {
        config.runtime = MobileRuntimeConfig::FromJson(json["runtime"]);
    }
    if (json.contains("models") && json["models"].is_object()) {
        config.models = MobileModelsConfig::FromJson(json["models"]);
    }
    if (json.contains("audio") && json["audio"].is_object()) {
        config.audio = MobileAudioConfig::FromJson(json["audio"]);
    }
    if (json.contains("vision") && json["vision"].is_object()) {
        config.vision = MobileVisionConfig::FromJson(json["vision"]);
    }
    if (json.contains("avatar") && json["avatar"].is_object()) {
        config.avatar = MobileAvatarConfig::FromJson(json["avatar"]);
    }
    return config;
}

SkillEntryConfig SkillEntryConfig::FromJson(const nlohmann::json& json) {
    SkillEntryConfig config;
    config.enabled = json.value("enabled", true);
    return config;
}

SkillsLoadConfig SkillsLoadConfig::FromJson(const nlohmann::json& json) {
    SkillsLoadConfig config;
    config.extra_dirs = json.value("extraDirs", std::vector<std::string>{});
    return config;
}

SkillsConfig SkillsConfig::FromJson(const nlohmann::json& json) {
    SkillsConfig config;

    // OpenClaw simple format: skills.path, skills.autoApprove, skills.configs
    config.path = json.value("path", "");
    config.auto_approve = json.value("autoApprove", std::vector<std::string>{});
    if (json.contains("configs") && json["configs"].is_object()) {
        config.configs = json["configs"];
    }

    // RavBot format: skills.load, skills.entries
    if (json.contains("load") && json["load"].is_object()) {
        config.load = SkillsLoadConfig::FromJson(json["load"]);
    }
    // OpenClaw path → extraDirs compatibility
    if (!config.path.empty() && config.load.extra_dirs.empty()) {
        config.load.extra_dirs.push_back(config.path);
    }

    if (json.contains("entries") && json["entries"].is_object()) {
        for (const auto& [key, value] : json["entries"].items()) {
            config.entries[key] = SkillEntryConfig::FromJson(value);
        }
    }
    return config;
}

GatewayConfig GatewayConfig::FromJson(const nlohmann::json& json) {
    GatewayConfig config;
    config.port = json.value("port", kDefaultGatewayPort);  // RavBot WebSocket RPC port
    config.bind = json.value("bind", "loopback");
    if (json.contains("auth")) {
        config.auth = GatewayAuthConfig::FromJson(json["auth"]);
    }
    if (json.contains("controlUi")) {
        config.control_ui = GatewayControlUiConfig::FromJson(json["controlUi"]);
    }

    // Health monitoring configuration (support both camelCase and snake_case)
    config.health_timeout_threshold_percent = json.value("healthTimeoutThresholdPercent",
                                                          json.value("health_timeout_threshold_percent", 80));
    config.health_max_timeout_count = json.value("healthMaxTimeoutCount",
                                                  json.value("health_max_timeout_count", 5));
    config.health_degraded_check_count = json.value("healthDegradedCheckCount",
                                                     json.value("health_degraded_check_count", 3));

    // Auth lockout configuration (support both camelCase and snake_case)
    config.auth_max_attempts = json.value("authMaxAttempts",
                                           json.value("auth_max_attempts", 5));
    config.auth_lockout_duration_sec = json.value("authLockoutDurationSec",
                                                   json.value("auth_lockout_duration_sec", 300));

    return config;
}

RavBotConfig RavBotConfig::FromJson(const nlohmann::json& json) {
    // Expand ${VAR} references in a mutable copy
    nlohmann::json expanded = json;
    expand_env_in_json(expanded);

    return FromJsonExpanded(expanded);
}

RavBotConfig RavBotConfig::FromJsonExpanded(const nlohmann::json& json) {
    RavBotConfig config;

    // ================================================================
    // OpenClaw "system" section → system config + gateway port
    // ================================================================
    if (json.contains("system") && json["system"].is_object()) {
        config.system = SystemConfig::FromJson(json["system"]);
        // system.port overrides gateway.controlUi.port (HTTP port)
        if (config.system.port > 0) {
            config.gateway.control_ui.port = config.system.port;
        }
    }

    // ================================================================
    // OpenClaw "llm" section → agent + providers (flat, single provider)
    // Format: { "provider": "openai", "model": "anthropic/claude-sonnet-4-6", "apiKey": "...", "baseUrl": "...", "temperature": 0.2, "maxTokens": 2048 }
    // ================================================================
    if (json.contains("llm") && json["llm"].is_object()) {
        const auto& llm = json["llm"];
        std::string provider_name = llm.value("provider", "openai");

        config.agent.model = llm.value("model", "anthropic/claude-sonnet-4-6");
        config.agent.temperature = llm.value("temperature", kDefaultTemperature);
        config.agent.max_tokens = llm.value("maxTokens", kDefaultMaxTokens);

        ProviderConfig prov;
        prov.api_key = llm.value("apiKey", "");
        prov.base_url = llm.value("baseUrl", "");
        prov.timeout = llm.value("timeout", kDefaultProviderTimeoutSec);
        config.providers[provider_name] = prov;
    }

    // ================================================================
    // RavBot "agent" section (takes priority over llm if both exist)
    // ================================================================
    if (json.contains("agent") && json["agent"].is_object()) {
        config.agent = AgentConfig::FromJson(json["agent"]);
    } else if (json.contains("agents") && json["agents"].contains("defaults")) {
        // Legacy format
        config.agent = AgentConfig::FromJson(json["agents"]["defaults"]);
    }

    // ================================================================
    // Gateway
    // ================================================================
    if (json.contains("gateway") && json["gateway"].is_object()) {
        config.gateway = GatewayConfig::FromJson(json["gateway"]);
    }

    // ================================================================
    // Providers (RavBot multi-provider format, merges with llm-derived provider)
    // ================================================================
    if (json.contains("providers") && json["providers"].is_object()) {
        for (const auto& [key, value] : json["providers"].items()) {
            config.providers[key] = ProviderConfig::FromJson(value);
        }
    }

    // ================================================================
    // Models providers (OpenClaw multi-model format: models.providers)
    // ================================================================
    if (json.contains("models") && json["models"].is_object() &&
        json["models"].contains("providers") && json["models"]["providers"].is_object()) {
        for (const auto& [id, val] : json["models"]["providers"].items()) {
            config.model_providers[id] = ProviderConfig::FromJson(val);
        }
    }

    // ================================================================
    // Model aliases (agents.defaults.models)
    // ================================================================
    if (json.contains("agents") && json["agents"].is_object() &&
        json["agents"].contains("defaults") && json["agents"]["defaults"].is_object() &&
        json["agents"]["defaults"].contains("models") && json["agents"]["defaults"]["models"].is_object()) {
        for (const auto& [key, val] : json["agents"]["defaults"]["models"].items()) {
            config.model_entries[key] = ModelEntryConfig::FromJson(val);
        }
    }

    // ================================================================
    // Agent model object form (agents.defaults.model as object with primary/fallbacks)
    // ================================================================
    if (json.contains("agents") && json["agents"].is_object() &&
        json["agents"].contains("defaults") && json["agents"]["defaults"].is_object() &&
        json["agents"]["defaults"].contains("model") && json["agents"]["defaults"]["model"].is_object()) {
        const auto& model_val = json["agents"]["defaults"]["model"];
        config.agent.model = model_val.value("primary", config.agent.model);
        if (model_val.contains("fallbacks") && model_val["fallbacks"].is_array()) {
            config.agent.fallbacks = model_val["fallbacks"].get<std::vector<std::string>>();
        }
    }

    // ================================================================
    // Channels — store full raw JSON per channel for adapter passthrough
    // ================================================================
    if (json.contains("channels") && json["channels"].is_object()) {
        for (const auto& [key, value] : json["channels"].items()) {
            config.channels[key] = ChannelConfig::FromJson(value);
        }
    }

    // ================================================================
    // Security (OpenClaw format)
    // ================================================================
    if (json.contains("security") && json["security"].is_object()) {
        config.security = SecurityConfig::FromJson(json["security"]);
    }

    // ================================================================
    // Mobile runtime
    // ================================================================
    if (json.contains("mobile") && json["mobile"].is_object()) {
        config.mobile = MobileConfig::FromJson(json["mobile"]);
    }

    // ================================================================
    // MCP
    // ================================================================
    if (json.contains("mcp") && json["mcp"].is_object()) {
        config.mcp = MCPConfig::FromJson(json["mcp"]);
    }

    // ================================================================
    // Skills (both OpenClaw and RavBot format)
    // ================================================================
    if (json.contains("skills") && json["skills"].is_object()) {
        config.skills = SkillsConfig::FromJson(json["skills"]);
    }

    // ================================================================
    // Plugins (raw JSON, consumed by PluginRegistry)
    // ================================================================
    if (json.contains("plugins") && json["plugins"].is_object()) {
        config.plugins_config = json["plugins"];
    }

    // ================================================================
    // Session maintenance
    // ================================================================
    if (json.contains("session") && json["session"].is_object()) {
        if (json["session"].contains("maintenance")) {
            config.session_maintenance_config = json["session"]["maintenance"];
        }
    }

    // ================================================================
    // Subagent config
    // ================================================================
    if (json.contains("subagents") && json["subagents"].is_object()) {
        config.subagent_config = json["subagents"];
    } else if (json.contains("agents") && json["agents"].is_object() &&
               json["agents"].contains("defaults") &&
               json["agents"]["defaults"].contains("subagents")) {
        config.subagent_config = json["agents"]["defaults"]["subagents"];
    }

    // ================================================================
    // Browser
    // ================================================================
    if (json.contains("browser") && json["browser"].is_object()) {
        config.browser_config = json["browser"];
    }

    // ================================================================
    // Queue (command queue config, consumed by CommandQueue)
    // ================================================================
    if (json.contains("queue") && json["queue"].is_object()) {
        config.queue_config = json["queue"];
    }

    // ================================================================
    // Tools (permission allow/deny or legacy named configs)
    // ================================================================
    if (json.contains("tools") && json["tools"].is_object()) {
        const auto& tools_json = json["tools"];

        // First, extract exec config before processing permissions
        if (tools_json.contains("exec") && tools_json["exec"].is_object()) {
            config.exec_approval_config = tools_json["exec"];
            spdlog::info("Config: exec_approval_config loaded: {}", config.exec_approval_config.dump());
        }

        // Then process tool permissions
        if (tools_json.contains("allow") || tools_json.contains("deny")) {
            config.tools_permission = ToolPermissionConfig::FromJson(tools_json);
        } else {
            for (const auto& [key, value] : tools_json.items()) {
                if (key != "exec" && key != "profile") {  // Skip exec and profile
                    config.tools[key] = ToolConfig::FromJson(value);
                }
            }
        }
    }

    return config;
}

// ---------------------------------------------------------------------------
// Dot-path config set/unset
// ---------------------------------------------------------------------------

static std::vector<std::string> split_dot_path(const std::string& path) {
    std::vector<std::string> parts;
    std::string::size_type start = 0;
    while (start < path.size()) {
        auto dot = path.find('.', start);
        if (dot == std::string::npos) {
            parts.push_back(path.substr(start));
            break;
        }
        parts.push_back(path.substr(start, dot - start));
        start = dot + 1;
    }
    return parts;
}

static nlohmann::json read_json_file(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        return nlohmann::json::object();
    }
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filepath);
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    std::string clean = strip_json5(content);
    return nlohmann::json::parse(clean);
}

static void write_json_file(const std::string& filepath,
                             const nlohmann::json& j) {
    auto parent = std::filesystem::path(filepath).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    if (std::filesystem::exists(filepath)) {
        std::filesystem::copy_file(
            filepath, filepath + ".bak",
            std::filesystem::copy_options::overwrite_existing);
    }
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot write config file: " + filepath);
    }
    file << j.dump(2) << std::endl;
}

void RavBotConfig::SetValue(const std::string& filepath,
                                const std::string& dot_path,
                                const nlohmann::json& value) {
    std::string expanded = ExpandHome(filepath);
    auto root = read_json_file(expanded);
    auto parts = split_dot_path(dot_path);
    if (parts.empty()) {
        throw std::runtime_error("Empty config path");
    }

    nlohmann::json* node = &root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!node->contains(parts[i]) || !(*node)[parts[i]].is_object()) {
            (*node)[parts[i]] = nlohmann::json::object();
        }
        node = &(*node)[parts[i]];
    }
    (*node)[parts.back()] = value;
    write_json_file(expanded, root);
}

void RavBotConfig::UnsetValue(const std::string& filepath,
                                  const std::string& dot_path) {
    std::string expanded = ExpandHome(filepath);
    auto root = read_json_file(expanded);
    auto parts = split_dot_path(dot_path);
    if (parts.empty()) {
        throw std::runtime_error("Empty config path");
    }

    nlohmann::json* node = &root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!node->contains(parts[i]) || !(*node)[parts[i]].is_object()) {
            return;  // Path doesn't exist
        }
        node = &(*node)[parts[i]];
    }
    node->erase(parts.back());
    write_json_file(expanded, root);
}

std::string RavBotConfig::ExpandHome(const std::string& path) {
    std::string expanded = path;
    if (expanded.size() >= 2 && expanded.substr(0, 2) == "~/") {
        const char* home = std::getenv("HOME");
#ifdef _WIN32
        if (!home) home = std::getenv("USERPROFILE");
#endif
        if (home) {
            expanded = std::string(home) + expanded.substr(1);
        }
    }
    return expanded;
}

std::string RavBotConfig::DefaultConfigPath() {
    return ExpandHome("~/.ravbot/ravbot.json");
}

RavBotConfig RavBotConfig::LoadFromFile(const std::string& filepath) {
    std::string expanded_path = ExpandHome(filepath);

    if (!std::filesystem::exists(expanded_path)) {
        throw std::runtime_error("Config file not found: " + expanded_path);
    }

    std::ifstream file(expanded_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + expanded_path);
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    std::string clean = strip_json5(content);
    nlohmann::json json = nlohmann::json::parse(clean);

    return FromJson(json);
}

int AgentConfig::DynamicMaxIterations() const {
    // Scale linearly: 32K → 32 iterations, 200K → 160 iterations
    if (context_window <= kContextWindow32K) return kMinMaxIterations;
    if (context_window >= kContextWindow200K) return kMaxMaxIterations;

    double ratio = static_cast<double>(context_window - kContextWindow32K)
                 / (kContextWindow200K - kContextWindow32K);
    return kMinMaxIterations +
           static_cast<int>(ratio * (kMaxMaxIterations - kMinMaxIterations));
}

// ---------------------------------------------------------------------------
// Requirements: 21.2, 21.5 - 配置验证
// ---------------------------------------------------------------------------

std::vector<std::string> RavBotConfig::Validate(const nlohmann::json& json) {
    std::vector<std::string> errors;

    // 验证根对象类型
    if (!json.is_object()) {
        errors.push_back("Root configuration must be a JSON object");
        return errors;
    }

    // 验证 agent 配置
    if (json.contains("agent")) {
        const auto& agent = json["agent"];
        if (!agent.is_object()) {
            errors.push_back("agent: must be an object");
        } else {
            // 验证 model 字段
            if (agent.contains("model") && !agent["model"].is_string() && !agent["model"].is_object()) {
                errors.push_back("agent.model: must be a string or object");
            }
            // 验证数值字段
            if (agent.contains("maxIterations") && !agent["maxIterations"].is_number_integer()) {
                errors.push_back("agent.maxIterations: must be an integer");
            }
            if (agent.contains("max_iterations") && !agent["max_iterations"].is_number_integer()) {
                errors.push_back("agent.max_iterations: must be an integer");
            }
            if (agent.contains("temperature") && !agent["temperature"].is_number()) {
                errors.push_back("agent.temperature: must be a number");
            }
            if (agent.contains("maxTokens") && !agent["maxTokens"].is_number_integer()) {
                errors.push_back("agent.maxTokens: must be an integer");
            }
            if (agent.contains("max_tokens") && !agent["max_tokens"].is_number_integer()) {
                errors.push_back("agent.max_tokens: must be an integer");
            }
            if (agent.contains("contextWindow") && !agent["contextWindow"].is_number_integer()) {
                errors.push_back("agent.contextWindow: must be an integer");
            }
            if (agent.contains("context_window") && !agent["context_window"].is_number_integer()) {
                errors.push_back("agent.context_window: must be an integer");
            }
            // 验证 thinking 字段
            if (agent.contains("thinking") && agent["thinking"].is_string()) {
                std::string thinking = agent["thinking"].get<std::string>();
                if (thinking != "off" && thinking != "low" && thinking != "medium" && thinking != "high") {
                    errors.push_back("agent.thinking: must be one of 'off', 'low', 'medium', 'high'");
                }
            }
            // 验证 fallbacks 字段
            if (agent.contains("fallbacks") && !agent["fallbacks"].is_array()) {
                errors.push_back("agent.fallbacks: must be an array");
            }
        }
    }

    // 验证 providers 配置
    if (json.contains("providers")) {
        const auto& providers = json["providers"];
        if (!providers.is_object()) {
            errors.push_back("providers: must be an object");
        } else {
            for (const auto& [key, value] : providers.items()) {
                if (!value.is_object()) {
                    errors.push_back("providers." + key + ": must be an object");
                    continue;
                }
                // 验证 apiKey 字段
                if (value.contains("apiKey") && !value["apiKey"].is_string()) {
                    errors.push_back("providers." + key + ".apiKey: must be a string");
                }
                if (value.contains("api_key") && !value["api_key"].is_string()) {
                    errors.push_back("providers." + key + ".api_key: must be a string");
                }
                // 验证 baseUrl 字段
                if (value.contains("baseUrl") && !value["baseUrl"].is_string()) {
                    errors.push_back("providers." + key + ".baseUrl: must be a string");
                }
                if (value.contains("base_url") && !value["base_url"].is_string()) {
                    errors.push_back("providers." + key + ".base_url: must be a string");
                }
                // 验证 timeout 字段
                if (value.contains("timeout") && !value["timeout"].is_number_integer()) {
                    errors.push_back("providers." + key + ".timeout: must be an integer");
                }
            }
        }
    }

    // 验证 gateway 配置
    if (json.contains("gateway")) {
        const auto& gateway = json["gateway"];
        if (!gateway.is_object()) {
            errors.push_back("gateway: must be an object");
        } else {
            // 验证 port 字段
            if (gateway.contains("port") && !gateway["port"].is_number_integer()) {
                errors.push_back("gateway.port: must be an integer");
            }
            // 验证 bind 字段
            if (gateway.contains("bind") && !gateway["bind"].is_string()) {
                errors.push_back("gateway.bind: must be a string");
            }
        }
    }

    // 验证 channels 配置
    if (json.contains("channels")) {
        const auto& channels = json["channels"];
        if (!channels.is_object()) {
            errors.push_back("channels: must be an object");
        } else {
            for (const auto& [key, value] : channels.items()) {
                if (!value.is_object()) {
                    errors.push_back("channels." + key + ": must be an object");
                    continue;
                }
                // 验证 enabled 字段
                if (value.contains("enabled") && !value["enabled"].is_boolean()) {
                    errors.push_back("channels." + key + ".enabled: must be a boolean");
                }
                // 验证 token 字段
                if (value.contains("token") && !value["token"].is_string()) {
                    errors.push_back("channels." + key + ".token: must be a string");
                }
                if (value.contains("botToken") && !value["botToken"].is_string()) {
                    errors.push_back("channels." + key + ".botToken: must be a string");
                }
            }
        }
    }

    // 验证 system 配置
    if (json.contains("system")) {
        const auto& system = json["system"];
        if (!system.is_object()) {
            errors.push_back("system: must be an object");
        } else {
            // 验证 logLevel 字段
            if (system.contains("logLevel") && system["logLevel"].is_string()) {
                std::string level = system["logLevel"].get<std::string>();
                if (level != "trace" && level != "debug" && level != "info" &&
                    level != "warn" && level != "error" && level != "critical" && level != "off") {
                    errors.push_back("system.logLevel: must be one of 'trace', 'debug', 'info', 'warn', 'error', 'critical', 'off'");
                }
            }
            // 验证 port 字段
            if (system.contains("port") && !system["port"].is_number_integer()) {
                errors.push_back("system.port: must be an integer");
            }
        }
    }

    // 验证 security 配置
    if (json.contains("security")) {
        const auto& security = json["security"];
        if (!security.is_object()) {
            errors.push_back("security: must be an object");
        } else {
            // 验证 permissionLevel 字段
            if (security.contains("permissionLevel") && security["permissionLevel"].is_string()) {
                std::string level = security["permissionLevel"].get<std::string>();
                if (level != "auto" && level != "strict" && level != "permissive") {
                    errors.push_back("security.permissionLevel: must be one of 'auto', 'strict', 'permissive'");
                }
            }
            // 验证 allowLocalExecute 字段
            if (security.contains("allowLocalExecute") && !security["allowLocalExecute"].is_boolean()) {
                errors.push_back("security.allowLocalExecute: must be a boolean");
            }
        }
    }

    // Validate mobile config
    if (json.contains("mobile")) {
        const auto& mobile = json["mobile"];
        if (!mobile.is_object()) {
            errors.push_back("mobile: must be an object");
        } else {
            if (mobile.contains("runtime") && !mobile["runtime"].is_object()) {
                errors.push_back("mobile.runtime: must be an object");
            }
            if (mobile.contains("models") && !mobile["models"].is_object()) {
                errors.push_back("mobile.models: must be an object");
            }
            if (mobile.contains("audio") && !mobile["audio"].is_object()) {
                errors.push_back("mobile.audio: must be an object");
            }
            if (mobile.contains("vision") && !mobile["vision"].is_object()) {
                errors.push_back("mobile.vision: must be an object");
            }
            if (mobile.contains("avatar") && !mobile["avatar"].is_object()) {
                errors.push_back("mobile.avatar: must be an object");
            }

            if (mobile.contains("runtime") && mobile["runtime"].is_object()) {
                const auto& runtime = mobile["runtime"];
                if (runtime.contains("enabled") && !runtime["enabled"].is_boolean()) {
                    errors.push_back("mobile.runtime.enabled: must be a boolean");
                }
                if (runtime.contains("mode") && runtime["mode"].is_string()) {
                    std::string mode = runtime["mode"].get<std::string>();
                    if (mode != "local" && mode != "remote") {
                        errors.push_back("mobile.runtime.mode: must be one of 'local', 'remote'");
                    }
                }
                if (runtime.contains("foregroundOnly") &&
                    !runtime["foregroundOnly"].is_boolean()) {
                    errors.push_back("mobile.runtime.foregroundOnly: must be a boolean");
                }
                if (runtime.contains("continuousVision") &&
                    !runtime["continuousVision"].is_boolean()) {
                    errors.push_back("mobile.runtime.continuousVision: must be a boolean");
                }
            }

            if (mobile.contains("audio") && mobile["audio"].is_object()) {
                const auto& audio = mobile["audio"];
                if (audio.contains("sampleRate") &&
                    !audio["sampleRate"].is_number_integer()) {
                    errors.push_back("mobile.audio.sampleRate: must be an integer");
                }
            }

            if (mobile.contains("vision") && mobile["vision"].is_object()) {
                const auto& vision = mobile["vision"];
                if (vision.contains("sampleFps") && !vision["sampleFps"].is_number()) {
                    errors.push_back("mobile.vision.sampleFps: must be a number");
                }
                if (vision.contains("persistRawFrames") &&
                    !vision["persistRawFrames"].is_boolean()) {
                    errors.push_back("mobile.vision.persistRawFrames: must be a boolean");
                }
            }

            if (mobile.contains("avatar") && mobile["avatar"].is_object()) {
                const auto& avatar = mobile["avatar"];
                if (avatar.contains("renderer") && avatar["renderer"].is_string()) {
                    std::string renderer = avatar["renderer"].get<std::string>();
                    if (renderer != "face2d") {
                        errors.push_back("mobile.avatar.renderer: must be 'face2d' in MVP");
                    }
                }
            }
        }
    }

    return errors;
}

// ---------------------------------------------------------------------------
// Requirements: 21.6 - 配置合并
// ---------------------------------------------------------------------------

nlohmann::json RavBotConfig::Merge(const nlohmann::json& base, const nlohmann::json& override) {
    // 使用 nlohmann::json 的 merge_patch 方法
    // 这实现了 RFC 7396 JSON Merge Patch 语义
    nlohmann::json result = base;
    result.merge_patch(override);
    return result;
}

// ---------------------------------------------------------------------------
// Requirements: 21.7 - 美化输出
// ---------------------------------------------------------------------------

std::string RavBotConfig::PrettyPrint(const nlohmann::json& json, int indent) {
    return json.dump(indent);
}

} // namespace ravbot
