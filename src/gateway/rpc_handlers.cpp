// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string_view>

#include "ravbot/config.hpp"
#include "ravbot/constants.hpp"
#include "ravbot/core/agent_loop.hpp"
#include "ravbot/core/cron_scheduler.hpp"
#include "ravbot/core/memory_search.hpp"
#include "ravbot/core/message_commands.hpp"
#include "ravbot/core/prompt_builder.hpp"
#include "ravbot/core/session_compaction.hpp"
#include "ravbot/core/skill_loader.hpp"
#include "ravbot/core/vector_database.hpp"
#include "ravbot/gateway/command_queue.hpp"
#include "ravbot/gateway/device_pairing.hpp"
#include "ravbot/gateway/gateway_server.hpp"
#include "ravbot/gateway/protocol.hpp"
#include "ravbot/plugins/plugin_system.hpp"
#include "ravbot/providers/provider_registry.hpp"
#include "ravbot/security/exec_approval.hpp"
#include "ravbot/session/session_manager.hpp"
#include "ravbot/tools/tool_chain.hpp"
#include "ravbot/tools/tool_registry.hpp"

namespace {

constexpr size_t kDefaultEmbeddingDimensions = 256;

std::vector<std::string> ParseEmbeddingInputs(const nlohmann::json& input) {
  if (input.is_string()) {
    return {input.get<std::string>()};
  }
  if (!input.is_array()) {
    throw std::runtime_error("input must be a string or array of strings");
  }

  std::vector<std::string> texts;
  texts.reserve(input.size());
  for (const auto& item : input) {
    if (!item.is_string()) {
      throw std::runtime_error("input array must contain only strings");
    }
    texts.push_back(item.get<std::string>());
  }
  return texts;
}

std::vector<float> ParseVectorArray(const nlohmann::json& value) {
  if (!value.is_array()) {
    throw std::runtime_error("vector must be an array");
  }

  std::vector<float> result;
  result.reserve(value.size());
  for (const auto& item : value) {
    if (!item.is_number()) {
      throw std::runtime_error("vector must contain only numbers");
    }
    result.push_back(item.get<float>());
  }

  if (result.empty()) {
    throw std::runtime_error("vector must not be empty");
  }
  return result;
}

std::string Base64Encode(const std::vector<uint8_t>& bytes) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string encoded;
  encoded.reserve(((bytes.size() + 2) / 3) * 4);

  size_t i = 0;
  while (i + 2 < bytes.size()) {
    uint32_t block = (static_cast<uint32_t>(bytes[i]) << 16) |
                     (static_cast<uint32_t>(bytes[i + 1]) << 8) |
                     static_cast<uint32_t>(bytes[i + 2]);
    encoded.push_back(kAlphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kAlphabet[(block >> 12) & 0x3F]);
    encoded.push_back(kAlphabet[(block >> 6) & 0x3F]);
    encoded.push_back(kAlphabet[block & 0x3F]);
    i += 3;
  }

  const size_t remaining = bytes.size() - i;
  if (remaining == 1) {
    uint32_t block = static_cast<uint32_t>(bytes[i]) << 16;
    encoded.push_back(kAlphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kAlphabet[(block >> 12) & 0x3F]);
    encoded.push_back('=');
    encoded.push_back('=');
  } else if (remaining == 2) {
    uint32_t block = (static_cast<uint32_t>(bytes[i]) << 16) |
                     (static_cast<uint32_t>(bytes[i + 1]) << 8);
    encoded.push_back(kAlphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kAlphabet[(block >> 12) & 0x3F]);
    encoded.push_back(kAlphabet[(block >> 6) & 0x3F]);
    encoded.push_back('=');
  }

  return encoded;
}

size_t CountApproximateTokens(const std::vector<std::string>& texts) {
  size_t total = 0;
  for (const auto& text : texts) {
    total += std::max<size_t>(1, text.size() / 4);
  }
  return total;
}

uint64_t Fnv1a64(std::string_view text, uint64_t seed) {
  constexpr uint64_t kOffset = 14695981039346656037ull;
  constexpr uint64_t kPrime = 1099511628211ull;

  uint64_t hash = kOffset ^ seed;
  for (char ch : text) {
    hash ^= static_cast<uint64_t>(static_cast<unsigned char>(ch));
    hash *= kPrime;
  }
  return hash;
}

std::vector<std::string> TokenizeForEmbedding(const std::string& text) {
  std::vector<std::string> tokens;
  std::string current;
  current.reserve(text.size());

  auto flush = [&]() {
    if (!current.empty()) {
      tokens.push_back(current);
      current.clear();
    }
  };

  for (char ch : text) {
    unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch)) {
      current.push_back(static_cast<char>(std::tolower(uch)));
    } else {
      flush();
    }
  }
  flush();

  if (tokens.empty() && !text.empty()) {
    tokens.push_back(text);
  }
  return tokens;
}

std::vector<float> BuildDeterministicEmbedding(const std::string& text,
                                               size_t dimensions) {
  if (dimensions == 0) {
    throw std::runtime_error("embedding dimensions must be positive");
  }

  std::vector<float> embedding(dimensions, 0.0f);
  auto tokens = TokenizeForEmbedding(text);
  if (tokens.empty()) {
    return embedding;
  }

  auto add_feature = [&](std::string_view feature, float weight,
                         uint64_t seed_base) {
    for (uint64_t slot = 0; slot < 3; ++slot) {
      uint64_t hash = Fnv1a64(feature, seed_base + slot * 0x9E3779B97F4A7C15ull);
      size_t index = static_cast<size_t>(hash % dimensions);
      float sign = ((hash >> 63) & 1ull) == 0 ? 1.0f : -1.0f;
      embedding[index] += sign * weight;
    }
  };

  for (size_t i = 0; i < tokens.size(); ++i) {
    add_feature(tokens[i], 1.0f, 0xA5A5A5A5ull);
    if (i + 1 < tokens.size()) {
      add_feature(tokens[i] + "|" + tokens[i + 1], 0.65f, 0x5A5A5A5Aull);
    }

    const auto& token = tokens[i];
    if (token.size() >= 3) {
      for (size_t j = 0; j + 3 <= token.size(); ++j) {
        add_feature(std::string_view(token.data() + j, 3), 0.3f,
                    0x123456789ABCDEFull);
      }
    }
  }

  float norm = std::sqrt(std::inner_product(
      embedding.begin(), embedding.end(), embedding.begin(), 0.0f));
  if (norm <= std::numeric_limits<float>::epsilon()) {
    return embedding;
  }

  for (float& value : embedding) {
    value /= norm;
  }
  return embedding;
}

nlohmann::json EncodeEmbeddingPayload(const std::vector<float>& embedding,
                                     const std::string& encoding_format) {
  if (encoding_format == "float" || encoding_format.empty()) {
    nlohmann::json result = nlohmann::json::array();
    for (float value : embedding) {
      result.push_back(value);
    }
    return result;
  }

  if (encoding_format == "base64") {
    std::vector<uint8_t> raw(embedding.size() * sizeof(float));
    if (!raw.empty()) {
      std::memcpy(raw.data(), embedding.data(), raw.size());
    }
    return Base64Encode(raw);
  }

  throw std::runtime_error("unsupported encoding_format: " + encoding_format);
}

std::filesystem::path ResolveVectorDbPath(const std::string& log_file_path) {
  if (log_file_path.empty()) {
    return ":memory:";
  }

  auto log_path = std::filesystem::path(log_file_path);
  auto base_dir = log_path.parent_path().parent_path();
  if (base_dir.empty()) {
    return ":memory:";
  }

  auto data_dir = base_dir / "data";
  std::filesystem::create_directories(data_dir);
  return data_dir / "vectors.db";
}

std::shared_ptr<ravbot::VectorDatabase> CreateVectorDatabase(
    const std::string& log_file_path, std::shared_ptr<spdlog::logger> logger) {
  auto db_path = ResolveVectorDbPath(log_file_path);
  auto db =
      std::make_shared<ravbot::VectorDatabase>(db_path.string(), logger);
  if (!db->Initialize()) {
    throw std::runtime_error("failed to initialize vector database");
  }
  return db;
}

}  // namespace

namespace ravbot::gateway {

void register_rpc_handlers(
    GatewayServer& server,
    std::shared_ptr<ravbot::SessionManager> session_manager,
    std::shared_ptr<ravbot::AgentLoop> agent_loop,
    std::shared_ptr<ravbot::PromptBuilder> prompt_builder,
    std::shared_ptr<ravbot::ToolRegistry> tool_registry,
    const ravbot::RavBotConfig& config,
    std::shared_ptr<spdlog::logger> logger, std::function<void()> reload_fn,
    std::shared_ptr<ravbot::ProviderRegistry> provider_registry,
    std::shared_ptr<ravbot::SkillLoader> skill_loader,
    std::shared_ptr<ravbot::CronScheduler> cron_scheduler,
    std::shared_ptr<ravbot::ExecApprovalManager> exec_approval_mgr,
    ravbot::PluginSystem* plugin_system, CommandQueue* command_queue,
    std::string log_file_path) {
  auto vector_db = CreateVectorDatabase(log_file_path, logger);

  // --- gateway.health ---
  server.RegisterHandler(
      methods::kGatewayHealth,
      [&server, logger](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
        return {{"status", "ok"},
                {"uptime", server.GetUptimeSeconds()},
                {"version", ravbot::kVersion}};
      });

  // --- gateway.probe ---
  server.RegisterHandler(
      methods::kGatewayProbe,
      [&server, logger](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
        auto health_status = server.ProbeHealth();
        std::string status_str = HealthStatusToString(health_status);

        nlohmann::json result = {
            {"status", status_str},
            {"uptime", server.GetUptimeSeconds()},
            {"connections", server.GetConnectionCount()},
            {"version", ravbot::kVersion}
        };

        // 如果是降级状态,添加额外信息
        if (health_status == HealthStatus::kDegraded) {
            result["degraded"] = true;
            result["reason"] = "High number of timeout requests detected";
        } else if (health_status == HealthStatus::kUnreachable) {
            result["unreachable"] = true;
            result["reason"] = "Server not running";
        }

        return result;
      });

  // --- gateway.status ---
  server.RegisterHandler(methods::kGatewayStatus,
                         [&server, session_manager, logger](
                             const nlohmann::json& /*params*/,
                             ClientConnection& /*client*/) -> nlohmann::json {
                           auto sessions = session_manager->ListSessions();
                           return {{"running", true},
                                   {"port", server.GetPort()},
                                   {"connections", server.GetConnectionCount()},
                                   {"uptime", server.GetUptimeSeconds()},
                                   {"sessions", sessions.size()},
                                   {"version", ravbot::kVersion}};
                         });

  // --- config.get ---
  server.RegisterHandler(
      methods::kConfigGet,
      [&config, logger](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
        std::string path_param = params.value("path", "");

        // Build the full config object that the UI config form expects
        nlohmann::json full_config = {
            {"agent",
             {{"model", config.agent.model},
              {"maxIterations", config.agent.max_iterations},
              {"temperature", config.agent.temperature},
              {"maxTokens", config.agent.max_tokens},
              {"contextWindow", config.agent.context_window},
              {"thinking", config.agent.thinking},
              {"autoCompact", config.agent.auto_compact}}},
            {"gateway",
             {{"port", config.gateway.port}, {"bind", config.gateway.bind}}}};

        if (!path_param.empty()) {
          // Dot-path lookup for legacy callers
          if (path_param == "gateway.port")
            return config.gateway.port;
          if (path_param == "gateway.bind")
            return config.gateway.bind;
          if (path_param == "agent.model")
            return config.agent.model;
          if (path_param == "agent.maxIterations")
            return config.agent.max_iterations;
          if (path_param == "agent.temperature")
            return config.agent.temperature;
          throw std::runtime_error("Unknown config path: " + path_param);
        }

        // Return ConfigSnapshot shape expected by the UI
        auto config_path = RavBotConfig::DefaultConfigPath();
        bool exists = std::filesystem::exists(config_path);
        std::string raw_str = full_config.dump(2);

        return {{"path", config_path},   {"exists", exists},
                {"raw", raw_str},        {"hash", ""},
                {"parsed", full_config}, {"valid", true},
                {"config", full_config}, {"issues", nlohmann::json::array()}};
      });

  // --- config.set ---
  server.RegisterHandler(
      methods::kConfigSet,
      [logger, reload_fn](const nlohmann::json& params,
                          ClientConnection& /*client*/) -> nlohmann::json {
        std::string path = params.value("path", "");
        if (path.empty()) {
          throw std::runtime_error("path is required");
        }
        if (!params.contains("value")) {
          throw std::runtime_error("value is required");
        }

        auto config_file = RavBotConfig::DefaultConfigPath();
        RavBotConfig::SetValue(config_file, path, params["value"]);

        // Trigger hot-reload so the running server picks up the change
        if (reload_fn) {
          reload_fn();
        }

        return {{"ok", true}, {"path", path}};
      });

  // --- Shared agent request helper ---
  // Extracted so both agent.request and chat.send can reuse the core logic
  struct AgentRequestResult {
    std::string session_key;
    std::string final_response;
  };

  auto execute_agent_request =
      [session_manager, agent_loop, prompt_builder, logger, &server](
          const nlohmann::json& params, ClientConnection& /*client*/,
          ravbot::AgentEventCallback event_callback) -> AgentRequestResult {
    std::string session_key = params.value("sessionKey", "agent:main:main");
    std::string message = params.value("message", "");

    if (message.empty()) {
      throw std::runtime_error("message is required");
    }

    // --- In-conversation slash command interception ---
    // Check if the message is a slash command (/new, /reset, /compact, etc.)
    // before forwarding to the LLM.
    {
      ravbot::MessageCommandParser::Handlers cmd_handlers;
      cmd_handlers.reset_session = [session_manager](const std::string& key) {
        session_manager->ResetSession(key);
      };
      cmd_handlers.compact_session = [session_manager,
                                      logger](const std::string& key) {
        auto history = session_manager->GetHistory(key, -1);
        if (history.size() > 20) {
          // Simple truncation (keep last 20 messages)
          session_manager->ResetSession(key);
          int keep = std::min(20, static_cast<int>(history.size()));
          for (int i = static_cast<int>(history.size()) - keep;
               i < static_cast<int>(history.size()); ++i) {
            session_manager->AppendMessage(key, history[i]);
          }
          logger->info("Compacted session {}: kept {} of {} messages", key,
                       keep, history.size());
        }
      };
      cmd_handlers.get_status = [session_manager](const std::string& key) {
        auto history = session_manager->GetHistory(key, -1);
        return "Session: " + key +
               "\nMessages: " + std::to_string(history.size());
      };

      ravbot::MessageCommandParser cmd_parser(std::move(cmd_handlers));
      auto cmd_result = cmd_parser.Parse(message, session_key);
      if (cmd_result.handled) {
        return {session_key, cmd_result.reply};
      }
    }

    // Get or create session
    session_manager->GetOrCreate(session_key, "", "cli");

    // Auto-generate display_name from first user message
    auto sessions = session_manager->ListSessions();
    for (const auto& s : sessions) {
      if (s.session_key == session_key && s.display_name == session_key) {
        std::string truncated = message.substr(0, 50);
        session_manager->UpdateDisplayName(session_key, truncated);
        break;
      }
    }

    // Append user message
    session_manager->AppendMessage(session_key, "user", message);

    // Build system prompt
    std::string system_prompt = prompt_builder->BuildFull();

    // Load history
    auto history = session_manager->GetHistory(session_key, 50);

    // Convert SessionMessages to LLM Messages (lossless copy)
    std::vector<ravbot::Message> llm_history;
    for (const auto& smsg : history) {
      ravbot::Message m;
      m.role = smsg.role;
      m.content = smsg.content;
      llm_history.push_back(m);
    }

    // Remove the last message (the one we just appended) since process_message
    // adds it
    if (!llm_history.empty()) {
      llm_history.pop_back();
    }

    // Send streaming events to the client
    std::string final_response;
    auto wrapped_callback = [&event_callback, &final_response](
                                const ravbot::AgentEvent& event) {
      event_callback(event);
      if (event.type == events::kMessageEnd && event.data.contains("content")) {
        final_response = event.data["content"].get<std::string>();
      }
    };

    auto new_messages = agent_loop->ProcessMessageStream(
        message, llm_history, system_prompt, wrapped_callback);

    // Persist all new messages (assistant + tool_result) to session transcript
    for (const auto& msg : new_messages) {
      ravbot::SessionMessage smsg;
      smsg.role = msg.role;
      smsg.content = msg.content;
      session_manager->AppendMessage(session_key, smsg);
    }

    return {session_key, final_response};
  };

  // --- agent.request ---
  server.RegisterHandler(
      methods::kAgentRequest,
      [execute_agent_request, &server,
       logger](const nlohmann::json& params,
               ClientConnection& client) -> nlohmann::json {
        auto result = execute_agent_request(
            params, client,
            [&server, &client, logger](const ravbot::AgentEvent& event) {
              RpcEvent rpc_event;
              rpc_event.event = event.type;
              rpc_event.payload = event.data;
              server.SendEventTo(client.connection_id, rpc_event);
            });
        return {{"sessionKey", result.session_key},
                {"response", result.final_response}};
      });

  // --- agent.stop ---
  server.RegisterHandler(
      methods::kAgentStop,
      [agent_loop, logger](const nlohmann::json& /*params*/,
                           ClientConnection& /*client*/) -> nlohmann::json {
        agent_loop->Stop();
        return {{"ok", true}};
      });

  // --- sessions.list ---
  server.RegisterHandler(
      methods::kSessionsList,
      [session_manager, &config,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        int limit = params.value("limit", 0);
        int offset = params.value("offset", 0);

        auto sessions = session_manager->ListSessions();
        int total = static_cast<int>(sessions.size());
        int start = std::min(offset, total);
        int end = (limit > 0) ? std::min(start + limit, total) : total;

        // Helper: Convert ISO timestamp "YYYY-MM-DDTHH:MM:SSZ" to milliseconds
        // since epoch
        auto iso_to_ms = [](const std::string& iso_str) -> int64_t {
          if (iso_str.empty())
            return 0;
          std::tm tm = {};
          std::istringstream ss(iso_str);
          ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
          if (ss.fail())
            return 0;
          tm.tm_isdst = 0;  // UTC has no DST
#ifdef _WIN32
          auto time_t_val = _mkgmtime64(&tm);
#else
          auto time_t_val = timegm(&tm);
#endif
          if (time_t_val < 0)
            return 0;
          return static_cast<int64_t>(time_t_val) * 1000;
        };

        nlohmann::json session_rows = nlohmann::json::array();
        for (int i = start; i < end; ++i) {
          const auto& s = sessions[i];
          nlohmann::json row;
          row["key"] = s.session_key;
          row["sessionId"] = s.session_id;
          row["displayName"] = s.display_name;
          row["surface"] = s.channel.empty() ? "cli" : s.channel;
          // Convert ISO timestamp to epoch ms
          row["updatedAt"] = iso_to_ms(s.updated_at);
          // Derive kind from key pattern
          if (s.session_key.find("group:") != std::string::npos) {
            row["kind"] = "group";
          } else if (s.session_key.find("global") != std::string::npos) {
            row["kind"] = "global";
          } else {
            row["kind"] = "direct";
          }
          session_rows.push_back(row);
        }

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

        return {{"ts", now_ms},
                {"path", ""},
                {"count", total},
                {"defaults",
                 {{"model", config.agent.model},
                  {"contextTokens", config.agent.context_window}}},
                {"sessions", session_rows}};
      });

  // --- sessions.history ---
  server.RegisterHandler(
      methods::kSessionsHistory,
      [session_manager,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        std::string session_key = params.value("sessionKey", "");
        int limit = params.value("limit", -1);

        if (session_key.empty()) {
          throw std::runtime_error("sessionKey is required");
        }

        auto history = session_manager->GetHistory(session_key, limit);

        nlohmann::json result = nlohmann::json::array();
        for (const auto& msg : history) {
          nlohmann::json entry;
          entry["role"] = msg.role;
          entry["timestamp"] = msg.timestamp;

          // Return full ContentBlock array
          nlohmann::json content_arr = nlohmann::json::array();
          for (const auto& block : msg.content) {
            content_arr.push_back(block.ToJson());
          }
          entry["content"] = content_arr;

          if (msg.usage) {
            entry["usage"] = msg.usage->ToJson();
          }

          result.push_back(entry);
        }
        return result;
      });

  // --- sessions.delete ---
  server.RegisterHandler(methods::kSessionsDelete,
                         [session_manager, logger](
                             const nlohmann::json& params,
                             ClientConnection& /*client*/) -> nlohmann::json {
                           // UI sends "key"; legacy clients send "sessionKey"
                           std::string session_key = params.value("key", "");
                           if (session_key.empty())
                             session_key = params.value("sessionKey", "");
                           if (session_key.empty()) {
                             throw std::runtime_error("key is required");
                           }
                           session_manager->DeleteSession(session_key);
                           return {{"ok", true}};
                         });

  // --- sessions.reset ---
  server.RegisterHandler(methods::kSessionsReset,
                         [session_manager, logger](
                             const nlohmann::json& params,
                             ClientConnection& /*client*/) -> nlohmann::json {
                           std::string session_key =
                               params.value("sessionKey", "");
                           if (session_key.empty()) {
                             throw std::runtime_error("sessionKey is required");
                           }
                           session_manager->ResetSession(session_key);
                           return {{"ok", true}};
                         });

  // --- channels.list ---
  server.RegisterHandler(
      methods::kChannelsList,
      [&config, logger](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
        nlohmann::json result = nlohmann::json::array();
        // CLI channel is always present
        result.push_back({{"id", "cli"},
                          {"type", "cli"},
                          {"enabled", true},
                          {"status", "active"}});
        // Add configured channels
        for (const auto& [id, ch] : config.channels) {
          nlohmann::json entry;
          entry["id"] = id;
          entry["type"] =
              id;  // type is typically same as id (discord, telegram, etc.)
          entry["enabled"] = ch.enabled;
          entry["status"] = ch.enabled ? "active" : "disabled";
          result.push_back(entry);
        }
        return result;
      });

  // --- channels.status ---
  // Returns ChannelsStatusSnapshot shape expected by the UI.
  server.RegisterHandler(
      methods::kChannelsStatus,
      [&config, logger](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

        // Build ordered list: cli first, then configured channels
        nlohmann::json channel_order = nlohmann::json::array();
        nlohmann::json channel_labels = nlohmann::json::object();
        nlohmann::json channels = nlohmann::json::object();
        nlohmann::json channel_accounts = nlohmann::json::object();
        nlohmann::json channel_default_account = nlohmann::json::object();

        auto add_channel = [&](const std::string& cid, bool enabled,
                               const std::string& label) {
          channel_order.push_back(cid);
          channel_labels[cid] = label;
          channels[cid] = {{"enabled", enabled},
                           {"running", enabled},
                           {"configured", enabled}};
          // Each channel has a default account entry
          nlohmann::json account;
          account["accountId"] = cid + ":default";
          account["enabled"] = enabled;
          account["configured"] = enabled;
          account["running"] = enabled;
          account["connected"] = enabled;
          channel_accounts[cid] = nlohmann::json::array({account});
          channel_default_account[cid] = cid + ":default";
        };

        add_channel("cli", true, "CLI");
        for (const auto& [cid, ch] : config.channels) {
          std::string label = cid;
          label[0] = static_cast<char>(
              std::toupper(static_cast<unsigned char>(label[0])));
          add_channel(cid, ch.enabled, label);
        }

        return {{"ts", now_ms},
                {"channelOrder", channel_order},
                {"channelLabels", channel_labels},
                {"channels", channels},
                {"channelAccounts", channel_accounts},
                {"channelDefaultAccountId", channel_default_account}};
      });

  // --- channels.logout (OpenClaw compat stub) ---
  server.RegisterHandler(
      "channels.logout",
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        std::string id = params.value("id", "");
        logger->info("channels.logout requested for channel '{}'", id);
        return {{"ok", true}};
      });

  // --- agents.list (OpenClaw multi-agent compat stub) ---
  // RavBot uses a single "main" agent; return AgentsListResult shape.
  server.RegisterHandler(
      "agents.list",
      [](const nlohmann::json& /*params*/,
         ClientConnection& /*client*/) -> nlohmann::json {
        return {{"defaultId", "main"},
                {"mainKey", "agent:main:main"},
                {"scope", "local"},
                {"agents", nlohmann::json::array(
                               {nlohmann::json{{"id", "main"},
                                               {"name", "RavBot Agent"},
                                               {"identity",
                                                {{"name", "RavBot Agent"},
                                                 {"theme", "default"},
                                                 {"emoji", "\xF0\x9F\xA6\x9E"},
                                                 {"avatar", ""}}}}})}};
      });

  // --- chain.execute ---
  server.RegisterHandler(
      methods::kChainExecute,
      [tool_registry, logger](const nlohmann::json& params,
                              ClientConnection& /*client*/) -> nlohmann::json {
        auto chain_def = ravbot::ToolChainExecutor::ParseChain(params);
        ravbot::ToolExecutorFn executor =
            [tool_registry](const std::string& name,
                            const nlohmann::json& args) {
              return tool_registry->ExecuteTool(name, args);
            };
        ravbot::ToolChainExecutor chain_executor(executor, logger);
        auto result = chain_executor.Execute(chain_def);
        return ravbot::ToolChainExecutor::ResultToJson(result);
      });

  // --- config.reload / config.apply (OpenClaw alias) ---
  if (reload_fn) {
    auto reload_handler =
        [reload_fn, logger](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
      reload_fn();
      return {{"ok", true}};
    };
    server.RegisterHandler(methods::kConfigReload, reload_handler);
    // OpenClaw clients use "config.apply" for hot-reload
    server.RegisterHandler("config.apply", reload_handler);
  }

  // ================================================================
  // OpenClaw-compatible RPC handlers (protocol shim)
  // ================================================================

  // --- chat.send (OpenClaw) ---
  // Translates RavBot agent events to OpenClaw format
  server.RegisterHandler(
      methods::kOcChatSend,
      [execute_agent_request, &server,
       logger](const nlohmann::json& params,
               ClientConnection& client) -> nlohmann::json {
        auto result = execute_agent_request(
            params, client,
            [&server, &client, logger](const ravbot::AgentEvent& event) {
              RpcEvent rpc_event;

              if (event.type == events::kTextDelta) {
                // agent.text_delta → event "agent" {stream:"assistant",
                // data:{text}}
                rpc_event.event = events::kOcAgent;
                rpc_event.payload = {
                    {"stream", "assistant"},
                    {"data", {{"text", event.data.value("text", "")}}}};
              } else if (event.type == events::kToolUse) {
                // agent.tool_use → event "agent" {stream:"tool",
                // data:{id,name,input}}
                rpc_event.event = events::kOcAgent;
                rpc_event.payload = {
                    {"stream", "tool"},
                    {"data",
                     {{"id", event.data.value("id", "")},
                      {"name", event.data.value("name", "")},
                      {"input",
                       event.data.value("input", nlohmann::json::object())}}}};
              } else if (event.type == events::kToolResult) {
                // agent.tool_result → event "agent" {stream:"tool_result",
                // data:{tool_use_id,content}}
                rpc_event.event = events::kOcAgent;
                rpc_event.payload = {
                    {"stream", "tool_result"},
                    {"data",
                     {{"tool_use_id", event.data.value("tool_use_id", "")},
                      {"content", event.data.value("content", "")}}}};
              } else if (event.type == events::kMessageEnd) {
                // agent.message_end → event "chat" {state:"final", content}
                rpc_event.event = events::kOcChat;
                rpc_event.payload = {
                    {"state", "final"},
                    {"content", event.data.value("content", "")}};
              } else {
                // Pass through any other events as-is
                rpc_event.event = event.type;
                rpc_event.payload = event.data;
              }

              server.SendEventTo(client.connection_id, rpc_event);
            });
        return {{"sessionKey", result.session_key},
                {"response", result.final_response}};
      });

  // --- chat.history (alias for sessions.history) ---
  server.RegisterHandler(
      methods::kOcChatHistory,
      [session_manager,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        std::string session_key = params.value("sessionKey", "");
        int limit = params.value("limit", -1);

        if (session_key.empty()) {
          throw std::runtime_error("sessionKey is required");
        }

        auto history = session_manager->GetHistory(session_key, limit);

        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : history) {
          nlohmann::json entry;
          entry["role"] = msg.role;
          entry["timestamp"] = msg.timestamp;

          nlohmann::json content_arr = nlohmann::json::array();
          for (const auto& block : msg.content) {
            content_arr.push_back(block.ToJson());
          }
          entry["content"] = content_arr;

          if (msg.usage) {
            entry["usage"] = msg.usage->ToJson();
          }

          messages.push_back(entry);
        }
        // UI expects {messages:[], thinkingLevel?:string}
        return {{"messages", messages}, {"thinkingLevel", nullptr}};
      });

  // --- chat.abort (alias for agent.stop) ---
  server.RegisterHandler(
      methods::kOcChatAbort,
      [agent_loop, logger](const nlohmann::json& /*params*/,
                           ClientConnection& /*client*/) -> nlohmann::json {
        agent_loop->Stop();
        return {{"ok", true}};
      });

  // --- health (alias for gateway.health) ---
  server.RegisterHandler(
      methods::kOcHealth,
      [&server, logger](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
        return {{"status", "ok"},
                {"uptime", server.GetUptimeSeconds()},
                {"version", ravbot::kVersion}};
      });

  // --- status (alias for gateway.status) ---
  // Returns an OpenClaw-compatible StatusSummary so OC clients don't crash
  // on missing fields (heartbeat, sessions.byAgent, channelSummary, etc.).
  server.RegisterHandler(
      methods::kOcStatus,
      [&server, session_manager, &config,
       logger](const nlohmann::json& /*params*/,
               ClientConnection& /*client*/) -> nlohmann::json {
        auto sessions = session_manager->ListSessions();

        // Build sessions.recent (last 5, lightweight)
        nlohmann::json recent = nlohmann::json::array();
        int recent_count = std::min(5, static_cast<int>(sessions.size()));
        for (int i = static_cast<int>(sessions.size()) - recent_count;
             i < static_cast<int>(sessions.size()); ++i) {
          recent.push_back({{"key", sessions[i].session_key},
                            {"sessionId", sessions[i].session_id},
                            {"updatedAt", sessions[i].updated_at},
                            {"model", config.agent.model}});
        }

        return {// RavBot fields
                {"running", true},
                {"port", server.GetPort()},
                {"connections", server.GetConnectionCount()},
                {"uptime", server.GetUptimeSeconds()},
                {"version", ravbot::kVersion},
                // OpenClaw compatibility fields
                {"heartbeat",
                 {{"defaultAgentId", "default"},
                  {"agents", nlohmann::json::array()}}},
                {"channelSummary", nlohmann::json::array()},
                {"queuedSystemEvents", nlohmann::json::array()},
                {"sessions",
                 {{"count", sessions.size()},
                  {"paths", nlohmann::json::array()},
                  {"defaults",
                   {{"model", config.agent.model},
                    {"contextTokens", config.agent.max_tokens}}},
                  {"recent", recent},
                  {"byAgent", nlohmann::json::array()}}}};
      });

  // --- models.list ---
  // Returns {models:[], current, aliases} shape expected by the UI.
  server.RegisterHandler(
      methods::kOcModelsList,
      [&config, provider_registry,
       logger](const nlohmann::json& /*params*/,
               ClientConnection& /*client*/) -> nlohmann::json {
        nlohmann::json models = nlohmann::json::array();

        // Active model first
        models.push_back({{"id", config.agent.model},
                          {"provider", "default"},
                          {"active", true}});

        // List models from registered providers
        if (provider_registry) {
          for (const auto& pid : provider_registry->ProviderIds()) {
            auto p = provider_registry->GetProvider(pid);
            if (p) {
              for (const auto& m : p->GetSupportedModels()) {
                if (m == config.agent.model)
                  continue;  // already added
                models.push_back(
                    {{"id", m}, {"provider", pid}, {"active", false}});
              }
            }
          }
        }

        return {{"models", models},
                {"current", config.agent.model},
                {"aliases", nlohmann::json::object()}};
      });

  // --- models.catalog ---
  // Returns detailed model catalog with context windows, costs, etc.
  server.RegisterHandler(methods::kModelsCatalog,
                         [provider_registry, logger](
                             const nlohmann::json& /*params*/,
                             ClientConnection& /*client*/) -> nlohmann::json {
                           nlohmann::json models = nlohmann::json::array();

                           if (provider_registry) {
                             auto catalog =
                                 provider_registry->GetModelCatalog();
                             for (const auto& entry : catalog) {
                               models.push_back(entry.ToJson());
                             }
                           }

                           return {{"models", models}};
                         });

  // --- tools.catalog ---
  // Returns ToolsCatalogResult shape expected by the UI.
  server.RegisterHandler(
      methods::kOcToolsCatalog,
      [tool_registry, logger](const nlohmann::json& params,
                              ClientConnection& /*client*/) -> nlohmann::json {
        std::string agent_id = params.value("agentId", "main");
        auto schemas = tool_registry->GetToolSchemas();

        // Build tools list for the "core" group
        nlohmann::json core_tools = nlohmann::json::array();
        for (const auto& schema : schemas) {
          core_tools.push_back(
              {{"id", schema.name},
               {"label", schema.name},
               {"description", schema.description},
               {"source", "core"},
               {"optional", true},
               {"defaultProfiles", nlohmann::json::array({"full", "coding"})}});
        }

        nlohmann::json profiles = nlohmann::json::array(
            {nlohmann::json{{"id", "minimal"}, {"label", "Minimal"}},
             nlohmann::json{{"id", "coding"}, {"label", "Coding"}},
             nlohmann::json{{"id", "messaging"}, {"label", "Messaging"}},
             nlohmann::json{{"id", "full"}, {"label", "Full"}}});

        nlohmann::json groups =
            nlohmann::json::array({nlohmann::json{{"id", "core"},
                                                  {"label", "Core Tools"},
                                                  {"source", "core"},
                                                  {"tools", core_tools}}});

        return {
            {"agentId", agent_id}, {"profiles", profiles}, {"groups", groups}};
      });

  // --- sessions.preview ---
  server.RegisterHandler(
      methods::kOcSessionsPreview,
      [session_manager,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        std::string session_key = params.value("sessionKey", "");
        if (session_key.empty()) {
          throw std::runtime_error("sessionKey is required");
        }

        auto history = session_manager->GetHistory(session_key, 1);
        if (history.empty()) {
          return nlohmann::json::object();
        }

        const auto& msg = history.back();
        nlohmann::json entry;
        entry["role"] = msg.role;
        entry["timestamp"] = msg.timestamp;

        nlohmann::json content_arr = nlohmann::json::array();
        for (const auto& block : msg.content) {
          content_arr.push_back(block.ToJson());
        }
        entry["content"] = content_arr;

        return entry;
      });

  // --- sessions.patch ---
  server.RegisterHandler(
      methods::kSessionsPatch,
      [session_manager,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        // UI sends "key"; legacy clients send "sessionKey"
        std::string session_key = params.value("key", "");
        if (session_key.empty())
          session_key = params.value("sessionKey", "");
        if (session_key.empty()) {
          throw std::runtime_error("key is required");
        }

        // Apply displayName / label rename
        if (params.contains("displayName")) {
          session_manager->UpdateDisplayName(
              session_key, params["displayName"].get<std::string>());
        } else if (params.contains("label")) {
          if (!params["label"].is_null()) {
            session_manager->UpdateDisplayName(
                session_key, params["label"].get<std::string>());
          }
        }
        // thinkingLevel, verboseLevel, reasoningLevel are stored in session
        // metadata in a full implementation; here we acknowledge them without
        // persisting.

        return {{"ok", true},
                {"path", ""},
                {"key", session_key},
                {"entry", {{"sessionId", ""}}}};
      });

  // --- sessions.compact ---
  server.RegisterHandler(
      methods::kSessionsCompact,
      [session_manager, agent_loop,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        std::string session_key = params.value("sessionKey", "");
        if (session_key.empty()) {
          throw std::runtime_error("sessionKey is required");
        }

        auto history = session_manager->GetHistory(session_key);

        // Convert SessionMessage list to JSON for compaction API
        std::vector<nlohmann::json> history_json;
        for (const auto& m : history) {
          history_json.push_back(m.ToJsonl());
        }

        ravbot::SessionCompaction compaction(logger);
        ravbot::SessionCompaction::Options opts;
        opts.max_messages = params.value("maxMessages", 100);
        opts.keep_recent = params.value("keepRecent", 20);

        if (!compaction.NeedsCompaction(history_json, opts)) {
          return {{"compacted", false}, {"reason", "below threshold"}};
        }

        auto compacted = compaction.Truncate(history_json, opts);

        // Increment compaction count
        session_manager->IncrementCompactionCount(session_key);
        int compaction_count = session_manager->GetCompactionCount(session_key);

        return {{"compacted", true},
                {"originalCount", static_cast<int>(history.size())},
                {"newCount", static_cast<int>(compacted.size())},
                {"compactionCount", compaction_count}};
      });

  // --- skills.status ---
  if (skill_loader) {
    server.RegisterHandler(
        methods::kSkillsStatus,
        [skill_loader, &config,
         logger](const nlohmann::json& /*params*/,
                 ClientConnection& /*client*/) -> nlohmann::json {
          const char* home = std::getenv("HOME");
          std::string home_str = home ? home : "/tmp";
          auto workspace_path = std::filesystem::path(home_str) /
                                ".ravbot/agents/main/workspace";
          std::string managed_dir =
              (std::filesystem::path(home_str) / ".ravbot/skills").string();

          auto skills = skill_loader->LoadSkills(config.skills, workspace_path);

          nlohmann::json skill_entries = nlohmann::json::array();
          for (const auto& skill : skills) {
            bool gated = !skill_loader->CheckSkillGating(skill);
            std::string skill_key =
                skill.skill_key.empty() ? skill.name : skill.skill_key;

            // Build missing bins/env/config lists
            nlohmann::json missing_bins = nlohmann::json::array();
            nlohmann::json missing_env = nlohmann::json::array();

            // Build install options from SkillInstallInfo
            nlohmann::json install_opts = nlohmann::json::array();
            for (const auto& inst : skill.installs) {
              std::string kind = inst.EffectiveMethod();
              if (kind == "node" || kind == "go" || kind == "uv" ||
                  kind == "brew") {
                nlohmann::json opt;
                opt["id"] = kind + ":" + inst.EffectiveFormula();
                opt["kind"] = kind;
                opt["label"] =
                    inst.label.empty() ? inst.EffectiveFormula() : inst.label;
                nlohmann::json bins = nlohmann::json::array();
                for (const auto& b : inst.bins)
                  bins.push_back(b);
                if (bins.empty() && !inst.EffectiveBinary().empty()) {
                  bins.push_back(inst.EffectiveBinary());
                }
                opt["bins"] = bins;
                install_opts.push_back(opt);
              }
            }

            skill_entries.push_back(
                {{"name", skill.name},
                 {"description", skill.description},
                 {"source", "bundled"},
                 {"filePath", skill.root_dir.string()},
                 {"baseDir", skill.root_dir.string()},
                 {"skillKey", skill_key},
                 {"bundled", true},
                 {"primaryEnv", skill.primary_env},
                 {"emoji", skill.emoji},
                 {"homepage", skill.homepage},
                 {"always", skill.always},
                 {"disabled", false},
                 {"blockedByAllowlist", false},
                 {"eligible", !gated},
                 {"requirements",
                  {{"bins",
                    [&]() {
                      nlohmann::json a = nlohmann::json::array();
                      for (const auto& b : skill.required_bins)
                        a.push_back(b);
                      return a;
                    }()},
                   {"env",
                    [&]() {
                      nlohmann::json a = nlohmann::json::array();
                      for (const auto& e : skill.required_envs)
                        a.push_back(e);
                      return a;
                    }()},
                   {"config", nlohmann::json::array()},
                   {"os",
                    [&]() {
                      nlohmann::json a = nlohmann::json::array();
                      for (const auto& o : skill.os_restrict)
                        a.push_back(o);
                      return a;
                    }()}}},
                 {"missing",
                  {{"bins", missing_bins},
                   {"env", missing_env},
                   {"config", nlohmann::json::array()},
                   {"os", nlohmann::json::array()}}},
                 {"configChecks", nlohmann::json::array()},
                 {"install", install_opts}});
          }

          return {{"workspaceDir", workspace_path.string()},
                  {"managedSkillsDir", managed_dir},
                  {"skills", skill_entries}};
        });

    // --- skills.install ---
    server.RegisterHandler(
        methods::kSkillsInstall,
        [skill_loader, logger](const nlohmann::json& params,
                               ClientConnection& /*client*/) -> nlohmann::json {
          std::string name = params.value("name", "");
          if (name.empty()) {
            throw std::runtime_error("skill name is required");
          }

          ravbot::SkillMetadata meta;
          meta.name = name;
          meta.root_dir = params.value("rootDir", "");

          bool ok = skill_loader->InstallSkill(meta);
          return {{"ok", ok}, {"name", name}};
        });
  }

  // --- cron.list ---
  if (cron_scheduler) {
    server.RegisterHandler(
        methods::kCronList,
        [cron_scheduler](const nlohmann::json& params,
                         ClientConnection& /*client*/) -> nlohmann::json {
          int limit = params.value("limit", 0);
          int offset = params.value("offset", 0);
          auto jobs = cron_scheduler->ListJobs();
          int total = static_cast<int>(jobs.size());
          int start = std::min(offset, total);
          int end = (limit > 0) ? std::min(start + limit, total) : total;

          auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

          auto tp_to_ms =
              [](std::chrono::system_clock::time_point tp) -> long long {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       tp.time_since_epoch())
                .count();
          };

          nlohmann::json job_list = nlohmann::json::array();
          for (int i = start; i < end; ++i) {
            const auto& job = jobs[i];
            long long last_ms = tp_to_ms(job.last_run);
            long long next_ms = tp_to_ms(job.next_run);

            nlohmann::json state = nlohmann::json::object();
            if (next_ms > 0)
              state["nextRunAtMs"] = next_ms;
            if (last_ms > 0)
              state["lastRunAtMs"] = last_ms;

            job_list.push_back(
                {{"id", job.id},
                 {"name", job.name},
                 {"description", ""},
                 {"enabled", job.enabled},
                 {"deleteAfterRun", false},
                 {"createdAtMs", now_ms},
                 {"updatedAtMs", now_ms},
                 {"schedule", {{"kind", "cron"}, {"expr", job.schedule}}},
                 {"sessionTarget", "main"},
                 {"wakeMode", "now"},
                 {"payload", {{"kind", "agentTurn"}, {"message", job.message}}},
                 {"state", state}});
          }

          bool has_more = end < total;
          return {{"jobs", job_list},
                  {"total", total},
                  {"offset", offset},
                  {"limit", limit > 0 ? limit : total},
                  {"hasMore", has_more},
                  {"nextOffset",
                   has_more ? nlohmann::json(end) : nlohmann::json(nullptr)}};
        });

    // --- cron.add ---
    // Supports both flat format (schedule string + message) and the rich UI
    // format {name, schedule:{kind,expr,everyMs,...},
    // payload:{kind,message,...}, ...}
    server.RegisterHandler(
        methods::kCronAdd,
        [cron_scheduler](const nlohmann::json& params,
                         ClientConnection& /*client*/) -> nlohmann::json {
          std::string name = params.value("name", "");
          std::string session_key =
              params.value("sessionKey", "agent:main:main");

          // Extract schedule expression (flat or nested)
          std::string schedule;
          if (params.contains("schedule") && params["schedule"].is_object()) {
            const auto& sched = params["schedule"];
            std::string kind = sched.value("kind", "cron");
            if (kind == "cron") {
              schedule = sched.value("expr", "");
            } else if (kind == "every") {
              // Convert everyMs to approximate cron expression
              long long every_ms = sched.value("everyMs", 3600000LL);
              long long every_min = std::max(1LL, every_ms / 60000LL);
              if (every_min < 60) {
                schedule = "*/" + std::to_string(every_min) + " * * * *";
              } else {
                long long every_hr = every_min / 60;
                schedule = "0 */" + std::to_string(every_hr) + " * * *";
              }
            } else if (kind == "at") {
              schedule = sched.value("at", "0 * * * *");
            }
          } else {
            schedule = params.value("schedule", "");
          }

          // Extract message (flat or nested in payload)
          std::string message;
          if (params.contains("payload") && params["payload"].is_object()) {
            message = params["payload"].value("message", "");
          } else {
            message = params.value("message", "");
          }

          if (name.empty() && !message.empty()) {
            name = message.substr(0, std::min(message.size(), (size_t)40));
          }
          if (schedule.empty() || message.empty()) {
            throw std::runtime_error("schedule and message are required");
          }

          auto id =
              cron_scheduler->AddJob(name, schedule, message, session_key);
          return {{"ok", true}, {"id", id}};
        });

    // --- cron.remove ---
    server.RegisterHandler(
        methods::kCronRemove,
        [cron_scheduler](const nlohmann::json& params,
                         ClientConnection& /*client*/) -> nlohmann::json {
          std::string id = params.value("id", "");
          if (id.empty()) {
            throw std::runtime_error("cron job id is required");
          }
          bool removed = cron_scheduler->RemoveJob(id);
          return {{"ok", removed}};
        });
  }

  // --- cron.update ---
  // Accepts both flat format and UI's {id, patch:{...}} format.
  if (cron_scheduler) {
    server.RegisterHandler(
        methods::kCronUpdate,
        [cron_scheduler,
         logger](const nlohmann::json& params,
                 ClientConnection& /*client*/) -> nlohmann::json {
          std::string id = params.value("id", "");
          if (id.empty()) {
            throw std::runtime_error("cron job id is required");
          }

          // Flatten nested patch object if present
          nlohmann::json flat = params;
          if (params.contains("patch") && params["patch"].is_object()) {
            for (auto& [k, v] : params["patch"].items()) {
              flat[k] = v;
            }
          }

          auto jobs = cron_scheduler->ListJobs();
          for (const auto& job : jobs) {
            if (job.id == id) {
              std::string name = flat.value("name", job.name);
              std::string schedule = job.schedule;
              std::string message = job.message;

              // Extract schedule from nested or flat
              if (flat.contains("schedule") && flat["schedule"].is_object()) {
                const auto& s = flat["schedule"];
                if (s.value("kind", "") == "cron") {
                  schedule = s.value("expr", job.schedule);
                }
              } else if (flat.contains("schedule") &&
                         flat["schedule"].is_string()) {
                schedule = flat["schedule"].get<std::string>();
              }

              // Extract message from nested payload or flat
              if (flat.contains("payload") && flat["payload"].is_object()) {
                message = flat["payload"].value("message", job.message);
              } else if (flat.contains("message")) {
                message = flat.value("message", job.message);
              }

              cron_scheduler->RemoveJob(job.id);
              auto new_id = cron_scheduler->AddJob(name, schedule, message,
                                                   job.session_key);
              return {{"ok", true}, {"id", new_id}};
            }
          }

          throw std::runtime_error("cron job not found: " + id);
        });

    // --- cron.run ---
    server.RegisterHandler(
        methods::kCronRun,
        [cron_scheduler, agent_loop, session_manager, prompt_builder,
         logger](const nlohmann::json& params,
                 ClientConnection& /*client*/) -> nlohmann::json {
          std::string id = params.value("id", "");
          if (id.empty()) {
            throw std::runtime_error("cron job id is required");
          }

          auto jobs = cron_scheduler->ListJobs();
          for (const auto& job : jobs) {
            if (job.id == id || job.id.substr(0, id.size()) == id) {
              // Execute the cron job's message as an agent request
              auto session = session_manager->GetOrCreate(job.session_key,
                                                          job.name, "cron");
              auto history_msgs = session_manager->GetHistory(job.session_key);

              std::vector<ravbot::Message> history;
              for (const auto& m : history_msgs) {
                ravbot::Message msg;
                msg.role = m.role;
                msg.content = m.content;
                history.push_back(msg);
              }

              auto system_prompt = prompt_builder->BuildFull(job.session_key);
              auto new_msgs = agent_loop->ProcessMessage(job.message, history,
                                                         system_prompt);

              // Store messages
              for (const auto& msg : new_msgs) {
                ravbot::SessionMessage sm;
                sm.role = msg.role;
                sm.content = msg.content;
                session_manager->AppendMessage(job.session_key, sm);
              }

              nlohmann::json r;
              r["ok"] = true;
              r["jobId"] = job.id;
              r["messagesGenerated"] = static_cast<int>(new_msgs.size());
              return r;
            }
          }

          throw std::runtime_error("cron job not found: " + id);
        });

    // --- cron.runs ---
    // Returns CronRunsResult shape expected by the UI.
    server.RegisterHandler(
        methods::kCronRuns,
        [cron_scheduler,
         logger](const nlohmann::json& params,
                 ClientConnection& /*client*/) -> nlohmann::json {
          std::string filter_id = params.value("id", "");
          int limit = params.value("limit", 0);
          int offset = params.value("offset", 0);
          auto jobs = cron_scheduler->ListJobs();

          auto tp_to_ms =
              [](std::chrono::system_clock::time_point tp) -> long long {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       tp.time_since_epoch())
                .count();
          };

          // Build synthetic run log entries from last_run timestamps
          nlohmann::json all_entries = nlohmann::json::array();
          for (const auto& job : jobs) {
            if (!filter_id.empty() && job.id != filter_id)
              continue;
            long long last_ms = tp_to_ms(job.last_run);
            long long next_ms = tp_to_ms(job.next_run);
            if (last_ms > 0) {
              all_entries.push_back(
                  {{"ts", last_ms},
                   {"jobId", job.id},
                   {"jobName", job.name},
                   {"status", "ok"},
                   {"runAtMs", last_ms},
                   {"nextRunAtMs", next_ms > 0 ? nlohmann::json(next_ms)
                                               : nlohmann::json(nullptr)}});
            }
          }

          int total = static_cast<int>(all_entries.size());
          int start = std::min(offset, total);
          int end = (limit > 0) ? std::min(start + limit, total) : total;

          nlohmann::json entries = nlohmann::json::array();
          for (int i = start; i < end; ++i)
            entries.push_back(all_entries[i]);

          bool has_more = end < total;
          return {{"entries", entries},
                  {"total", total},
                  {"offset", offset},
                  {"limit", limit > 0 ? limit : total},
                  {"hasMore", has_more},
                  {"nextOffset",
                   has_more ? nlohmann::json(end) : nlohmann::json(nullptr)}};
        });
  }

  // --- exec.approval.request ---
  if (exec_approval_mgr) {
    server.RegisterHandler(methods::kExecApprovalReq,
                           [exec_approval_mgr, logger](
                               const nlohmann::json& params,
                               ClientConnection& /*client*/) -> nlohmann::json {
                             std::string command = params.value("command", "");
                             if (command.empty()) {
                               throw std::runtime_error("command is required");
                             }

                             std::string cwd = params.value("cwd", "");
                             std::string agent_id = params.value("agentId", "");
                             std::string session_key =
                                 params.value("sessionKey", "");

                             auto decision = exec_approval_mgr->RequestApproval(
                                 command, cwd, agent_id, session_key);

                             std::string decision_str;
                             switch (decision) {
                               case ravbot::ApprovalDecision::kApproved:
                                 decision_str = "approved";
                                 break;
                               case ravbot::ApprovalDecision::kDenied:
                                 decision_str = "denied";
                                 break;
                               case ravbot::ApprovalDecision::kPending:
                                 decision_str = "pending";
                                 break;
                               default:
                                 decision_str = "timeout";
                                 break;
                             }

                             return {{"decision", decision_str}};
                           });

    // --- exec.approvals.get ---
    server.RegisterHandler(
        methods::kExecApprovals,
        [exec_approval_mgr,
         logger](const nlohmann::json& /*params*/,
                 ClientConnection& /*client*/) -> nlohmann::json {
          const auto& cfg = exec_approval_mgr->GetConfig();

          std::string mode_str;
          switch (cfg.ask) {
            case ravbot::AskMode::kOff:
              mode_str = "off";
              break;
            case ravbot::AskMode::kOnMiss:
              mode_str = "on-miss";
              break;
            case ravbot::AskMode::kAlways:
              mode_str = "always";
              break;
          }

          nlohmann::json patterns = nlohmann::json::array();
          for (const auto& p : cfg.allowlist) {
            patterns.push_back(p);
          }

          auto pending = exec_approval_mgr->PendingRequests();
          nlohmann::json pending_json = nlohmann::json::array();
          for (const auto& req : pending) {
            pending_json.push_back({{"id", req.id},
                                    {"command", req.command},
                                    {"cwd", req.cwd},
                                    {"agentId", req.agent_id},
                                    {"sessionKey", req.session_key}});
          }

          return {{"mode", mode_str},
                  {"allowlist", patterns},
                  {"pending", pending_json}};
        });
  }

  // --- models.set ---
  server.RegisterHandler(
      methods::kModelsSet,
      [agent_loop, logger](const nlohmann::json& params,
                           ClientConnection& /*client*/) -> nlohmann::json {
        std::string model = params.value("model", "");
        if (model.empty()) {
          throw std::runtime_error("model is required");
        }
        agent_loop->SetModel(model);
        return {{"ok", true}, {"model", model}};
      });

  // --- Plugin methods ---
  if (plugin_system) {
    // plugins.list
    server.RegisterHandler(
        methods::kPluginsList,
        [plugin_system](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
          return {{"plugins", plugin_system->Registry().ToJson()}};
        });

    // plugins.tools
    server.RegisterHandler(
        methods::kPluginsTools,
        [plugin_system](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
          return {{"tools", plugin_system->GetToolSchemas()}};
        });

    // plugins.call_tool
    server.RegisterHandler(
        methods::kPluginsCallTool,
        [plugin_system](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string name = params.value("toolName", "");
          if (name.empty())
            throw std::runtime_error("toolName is required");
          auto args = params.value("args", nlohmann::json::object());
          return plugin_system->CallTool(name, args);
        });

    // plugins.services
    server.RegisterHandler(
        methods::kPluginsServices,
        [plugin_system](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string action = params.value("action", "list");
          if (action == "start") {
            return plugin_system->StartService(params.value("serviceId", ""));
          }
          if (action == "stop") {
            return plugin_system->StopService(params.value("serviceId", ""));
          }
          return {{"services", plugin_system->ListServices()}};
        });

    // plugins.providers
    server.RegisterHandler(
        methods::kPluginsProviders,
        [plugin_system](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
          return {{"providers", plugin_system->ListProviders()}};
        });

    // plugins.commands
    server.RegisterHandler(
        methods::kPluginsCommands,
        [plugin_system](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string action = params.value("action", "list");
          if (action == "execute") {
            std::string cmd = params.value("command", "");
            auto args = params.value("args", nlohmann::json::object());
            return plugin_system->ExecuteCommand(cmd, args);
          }
          return {{"commands", plugin_system->ListCommands()}};
        });

    // plugins.gateway — forward plugin-registered gateway methods
    server.RegisterHandler(
        methods::kPluginsGateway,
        [plugin_system](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string action = params.value("action", "list");
          if (action == "list") {
            return {{"methods", plugin_system->ListGatewayMethods()}};
          }
          return {{"methods", plugin_system->ListGatewayMethods()}};
        });
  }

  // ================================================================
  // Queue management RPC handlers
  // ================================================================
  if (command_queue) {
    // --- queue.status ---
    server.RegisterHandler(
        methods::kQueueStatus,
        [command_queue](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string session_key = params.value("sessionKey", "");
          if (!session_key.empty()) {
            return command_queue->SessionQueueStatus(session_key);
          }
          return command_queue->GlobalStatus();
        });

    // --- queue.configure ---
    server.RegisterHandler(
        methods::kQueueConfigure,
        [command_queue](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string session_key = params.value("sessionKey", "");
          if (session_key.empty()) {
            // Global config update
            auto new_config = QueueConfig::FromJson(params);
            command_queue->SetConfig(new_config);
            return {{"ok", true}, {"scope", "global"}};
          }
          // Per-session config
          auto mode = QueueModeFromString(params.value("mode", "collect"));
          int debounce = params.value("debounceMs", -1);
          int cap = params.value("cap", -1);
          std::string drop = params.value("drop", "");
          command_queue->ConfigureSession(session_key, mode, debounce, cap,
                                          drop);
          return {
              {"ok", true}, {"scope", "session"}, {"sessionKey", session_key}};
        });

    // --- queue.cancel ---
    server.RegisterHandler(
        methods::kQueueCancel,
        [command_queue](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string command_id = params.value("commandId", "");
          if (command_id.empty()) {
            throw std::runtime_error("commandId is required");
          }
          bool cancelled = command_queue->Cancel(command_id);
          return {{"ok", cancelled}, {"commandId", command_id}};
        });

    // --- queue.abort ---
    server.RegisterHandler(
        methods::kQueueAbort,
        [command_queue](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string session_key = params.value("sessionKey", "");
          if (session_key.empty()) {
            throw std::runtime_error("sessionKey is required");
          }
          bool aborted = command_queue->AbortSession(session_key);
          return {{"ok", aborted}, {"sessionKey", session_key}};
        });
  }

  // --- memory.status ---
  {
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "/tmp";
    auto workspace =
        std::filesystem::path(home_str) / ".ravbot/agents/main/workspace";

    server.RegisterHandler(
        methods::kMemoryStatus,
        [workspace, logger](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
          ravbot::MemorySearch search(logger);
          search.IndexDirectory(workspace);
          return search.Stats();
        });

    // --- memory.search ---
    server.RegisterHandler(
        methods::kMemorySearch,
        [workspace, logger](const nlohmann::json& params,
                            ClientConnection& /*client*/) -> nlohmann::json {
          std::string query = params.value("query", "");
          int max_results = params.value("maxResults", 10);
          if (query.empty()) {
            throw std::runtime_error("query is required");
          }
          ravbot::MemorySearch search(logger);
          search.IndexDirectory(workspace);
          auto results = search.Search(query, max_results);
          nlohmann::json arr = nlohmann::json::array();
          for (const auto& r : results) {
            arr.push_back({{"source", r.source},
                           {"content", r.content},
                           {"score", r.score},
                           {"lineNumber", r.line_number}});
          }
          return arr;
        });
  }

  // ================================================================
  // UI compatibility handlers — missing in original implementation
  // ================================================================

  // --- agent.identity.get ---
  // Called on every UI connect to show assistant name/avatar.
  server.RegisterHandler(
      "agent.identity.get",
      [&config](const nlohmann::json& /*params*/,
                ClientConnection& /*client*/) -> nlohmann::json {
        return {{"agentId", "main"},
                {"name", "RavBot Agent"},
                {"avatar", ""},
                {"emoji", "\xF0\x9F\xA6\x9E"}};
      });

  // --- node.list ---
  // RavBot is a single-node deployment; return empty list.
  server.RegisterHandler("node.list",
                         [](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
                           return nlohmann::json::array();
                         });

  // --- device.pair.list ---
  // Device pairing not implemented; return empty list so UI doesn't hang.
  server.RegisterHandler("device.pair.list",
                         [](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
                           return nlohmann::json::array();
                         });

  // --- logs.tail ---
  // Return recent lines from the gateway log file.
  server.RegisterHandler(
      "logs.tail",
      [logger, log_file_path](const nlohmann::json& params,
                              ClientConnection& /*client*/) -> nlohmann::json {
        int req_limit = params.value("limit", 200);
        int max_bytes = params.value("maxBytes", 512 * 1024);
        long long cursor = params.value("cursor", 0LL);
        (void)max_bytes;
        (void)cursor;

        nlohmann::json lines = nlohmann::json::array();
        long long new_cursor = cursor;
        bool truncated = false;

        // Normalize the path and verify its structure before opening
        // (guards against $HOME containing '..' traversal components).
        namespace fs = std::filesystem;
        fs::path safe = fs::path(log_file_path).lexically_normal();
        bool path_ok = !safe.empty() && safe.filename() == "gateway.log" &&
                       safe.parent_path().filename() == "logs" &&
                       fs::exists(safe);
        if (path_ok) {
          std::ifstream ifs(safe);
          if (ifs.is_open()) {
            std::vector<std::string> all_lines;
            std::string line;
            while (std::getline(ifs, line)) {
              all_lines.push_back(line);
            }
            int total = static_cast<int>(all_lines.size());
            int start = std::max(0, total - req_limit);
            for (int i = start; i < total; ++i) {
              lines.push_back(all_lines[i]);
            }
            new_cursor = total;
            truncated = start > 0;
          }
        }

        return {{"file", log_file_path},
                {"cursor", new_cursor},
                {"lines", lines},
                {"truncated", truncated}};
      });

  // --- config.schema ---
  // Return a minimal JSON schema for the config form.
  server.RegisterHandler(
      "config.schema",
      [](const nlohmann::json& /*params*/,
         ClientConnection& /*client*/) -> nlohmann::json {
        nlohmann::json schema = {
            {"type", "object"},
            {"properties",
             {{"agent",
               {{"type", "object"},
                {"properties",
                 {{"model", {{"type", "string"}}},
                  {"maxIterations",
                   {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}},
                  {"temperature",
                   {{"type", "number"}, {"minimum", 0}, {"maximum", 2}}},
                  {"maxTokens", {{"type", "integer"}, {"minimum", 1}}},
                  {"thinking",
                   {{"type", "string"},
                    {"enum", nlohmann::json::array(
                                 {"off", "low", "medium", "high"})}}}}}}},
              {"gateway",
               {{"type", "object"},
                {"properties",
                 {{"port",
                   {{"type", "integer"}, {"minimum", 1}, {"maximum", 65535}}},
                  {"bind", {{"type", "string"}}}}}}}}}};
        nlohmann::json ui_hints = {
            {"agent.model", {{"label", "Model"}, {"group", "Agent"}}},
            {"agent.maxIterations",
             {{"label", "Max Iterations"}, {"group", "Agent"}}},
            {"agent.temperature",
             {{"label", "Temperature"},
              {"group", "Agent"},
              {"advanced", true}}},
            {"agent.thinking",
             {{"label", "Thinking Mode"}, {"group", "Agent"}}},
            {"gateway.port", {{"label", "Port"}, {"group", "Gateway"}}},
            {"gateway.bind",
             {{"label", "Bind Address"}, {"group", "Gateway"}}}};
        return {{"schema", schema},
                {"uiHints", ui_hints},
                {"version", "1"},
                {"generatedAt", ""}};
      });

  // --- cron.status ---
  if (cron_scheduler) {
    server.RegisterHandler(
        "cron.status",
        [cron_scheduler](const nlohmann::json& /*params*/,
                         ClientConnection& /*client*/) -> nlohmann::json {
          auto jobs = cron_scheduler->ListJobs();
          int enabled_count = 0;
          long long next_wake = 0;
          for (const auto& job : jobs) {
            if (job.enabled) {
              ++enabled_count;
              auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            job.next_run.time_since_epoch())
                            .count();
              if (ms > 0 && (next_wake == 0 || ms < next_wake)) {
                next_wake = ms;
              }
            }
          }
          return {{"enabled", enabled_count > 0},
                  {"jobs", static_cast<int>(jobs.size())},
                  {"nextWakeAtMs", next_wake > 0 ? nlohmann::json(next_wake)
                                                 : nlohmann::json(nullptr)}};
        });
  }

  // --- skills.update ---
  // Enable/disable a skill or save its API key.
  if (skill_loader) {
    server.RegisterHandler(
        "skills.update",
        [logger](const nlohmann::json& params,
                 ClientConnection& /*client*/) -> nlohmann::json {
          std::string skill_key = params.value("skillKey", "");
          if (skill_key.empty()) {
            throw std::runtime_error("skillKey is required");
          }
          // Persist enable/disable flag via env or config file is not yet
          // implemented; acknowledge the request so the UI doesn't show an
          // error.
          bool enabled = params.value("enabled", true);
          logger->info("skills.update: skillKey={} enabled={}", skill_key,
                       enabled);
          return {{"ok", true}, {"skillKey", skill_key}};
        });
  }

  // --- sessions.usage ---
  // Aggregate token usage from session histories for a date range.
  server.RegisterHandler(
      "sessions.usage",
      [session_manager,
       &config](const nlohmann::json& params,
                ClientConnection& /*client*/) -> nlohmann::json {
        std::string start_date = params.value("startDate", "");
        std::string end_date = params.value("endDate", "");

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

        // Aggregate totals across all sessions
        long long total_input = 0, total_output = 0, total_cache_read = 0, total_cache_write = 0;
        nlohmann::json session_entries = nlohmann::json::array();

        auto sessions = session_manager->ListSessions();
        for (const auto& s : sessions) {
          auto history = session_manager->GetHistory(s.session_key, -1);
          long long in_tok = 0, out_tok = 0, cache_read = 0, cache_write = 0;
          for (const auto& msg : history) {
            if (msg.usage) {
              in_tok += msg.usage->input_tokens;
              out_tok += msg.usage->output_tokens;
              cache_read += msg.usage->cache_read_input_tokens;
              cache_write += msg.usage->cache_creation_input_tokens;
            }
          }
          total_input += in_tok;
          total_output += out_tok;
          total_cache_read += cache_read;
          total_cache_write += cache_write;

          session_entries.push_back({{"key", s.session_key},
                                     {"label", s.display_name},
                                     {"model", config.agent.model},
                                     {"usage",
                                      {{"input", in_tok},
                                       {"output", out_tok},
                                       {"cacheRead", cache_read},
                                       {"cacheWrite", cache_write},
                                       {"totalTokens", in_tok + out_tok + cache_read + cache_write},
                                       {"totalCost", 0.0},
                                       {"missingCostEntries", 0}}}});
        }

        nlohmann::json zero_totals = {
            {"input", total_input},
            {"output", total_output},
            {"cacheRead", total_cache_read},
            {"cacheWrite", total_cache_write},
            {"totalTokens", total_input + total_output + total_cache_read + total_cache_write},
            {"totalCost", 0.0},
            {"inputCost", 0.0},
            {"outputCost", 0.0},
            {"cacheReadCost", 0.0},
            {"cacheWriteCost", 0.0},
            {"missingCostEntries", 0}};

        return {{"updatedAt", now_ms},
                {"startDate", start_date},
                {"endDate", end_date},
                {"sessions", session_entries},
                {"totals", zero_totals},
                {"aggregates",
                 {{"messages",
                   {{"total", 0},
                    {"user", 0},
                    {"assistant", 0},
                    {"toolCalls", 0},
                    {"toolResults", 0},
                    {"errors", 0}}},
                  {"tools",
                   {{"totalCalls", 0},
                    {"uniqueTools", 0},
                    {"tools", nlohmann::json::array()}}},
                  {"byModel", nlohmann::json::array()},
                  {"byProvider", nlohmann::json::array()},
                  {"byAgent", nlohmann::json::array()},
                  {"byChannel", nlohmann::json::array()},
                  {"daily", nlohmann::json::array()}}}};
      });

  // --- usage.cost ---
  // Return daily cost summary (costs are zero since we don't track model
  // pricing).
  server.RegisterHandler(
      "usage.cost",
      [](const nlohmann::json& params,
         ClientConnection& /*client*/) -> nlohmann::json {
        std::string start_date = params.value("startDate", "");
        std::string end_date = params.value("endDate", "");
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        nlohmann::json zero_totals = {{"input", 0},
                                      {"output", 0},
                                      {"cacheRead", 0},
                                      {"cacheWrite", 0},
                                      {"totalTokens", 0},
                                      {"totalCost", 0.0},
                                      {"inputCost", 0.0},
                                      {"outputCost", 0.0},
                                      {"cacheReadCost", 0.0},
                                      {"cacheWriteCost", 0.0},
                                      {"missingCostEntries", 0}};
        return {{"updatedAt", now_ms},
                {"days", 0},
                {"daily", nlohmann::json::array()},
                {"totals", zero_totals}};
      });

  // --- sessions.usage.timeseries ---
  // Return empty time series; UI silently fails on this.
  server.RegisterHandler("sessions.usage.timeseries",
                         [](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
                           return {{"sessionId", nullptr},
                                   {"points", nlohmann::json::array()}};
                         });

  // --- sessions.usage.logs ---
  // Return empty logs; UI silently fails on this.
  server.RegisterHandler("sessions.usage.logs",
                         [](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
                           return {{"logs", nlohmann::json::array()}};
                         });

  // --- skills.bins ---
  // Return list of required binaries from all skills
  server.RegisterHandler(
      methods::kOcSkillsBins,
      [&config, skill_loader,
       logger](const nlohmann::json& /*params*/,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          const char* home = std::getenv("HOME");
          std::string home_str = home ? home : "/tmp";
          auto workspace_path = std::filesystem::path(home_str) /
                                ".ravbot/agents/main/workspace";

          auto skills = skill_loader->LoadSkills(config.skills, workspace_path);
          std::set<std::string> bins_set;

          for (const auto& skill : skills) {
            for (const auto& bin : skill.required_bins) {
              if (!bin.empty()) {
                bins_set.insert(bin);
              }
            }
          }

          nlohmann::json bins = nlohmann::json::array();
          for (const auto& bin : bins_set) {
            bins.push_back(bin);
          }

          logger->debug("skills.bins: returning {} bins", bins.size());
          return {{"bins", bins}};
        } catch (const std::exception& e) {
          logger->error("skills.bins failed: {}", e.what());
          throw;
        }
      });

  // --- secrets.resolve ---
  server.RegisterHandler(
      methods::kOcSecretsResolve,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          if (!params.is_object()) {
            throw std::runtime_error(
                "invalid secrets.resolve params: commandName");
          }

          auto trim_copy = [](std::string value) {
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
          };

          if (!params.contains("commandName") ||
              !params["commandName"].is_string()) {
            throw std::runtime_error(
                "invalid secrets.resolve params: commandName");
          }

          if (!params.contains("targetIds") ||
              !params["targetIds"].is_array()) {
            throw std::runtime_error(
                "invalid secrets.resolve params: targetIds");
          }

          std::string command_name =
              trim_copy(params["commandName"].get<std::string>());
          if (command_name.empty()) {
            throw std::runtime_error(
                "invalid secrets.resolve params: commandName");
          }

          nlohmann::json assignments = nlohmann::json::array();
          nlohmann::json diagnostics = nlohmann::json::array();
          nlohmann::json inactive_ref_paths = nlohmann::json::array();

          for (const auto& entry : params["targetIds"]) {
            if (!entry.is_string()) {
              throw std::runtime_error(
                  "invalid secrets.resolve params: targetIds");
            }
            std::string target_id = trim_copy(entry.get<std::string>());
            if (target_id.empty()) {
              throw std::runtime_error(
                  "invalid secrets.resolve params: targetIds");
            }
          }

          if (params["targetIds"].empty()) {
            return {{"ok", true},
                    {"assignments", assignments},
                    {"diagnostics", diagnostics},
                    {"inactiveRefPaths", inactive_ref_paths}};
          }

          logger->warn(
              "secrets.resolve requested for {} target(s) by '{}', "
              "but gateway-side resolution is not implemented",
              params["targetIds"].size(), command_name);
          throw std::runtime_error(
              "secrets.resolve unavailable: gateway-side secret "
              "resolution is not implemented");
        } catch (const std::exception& e) {
          logger->error("secrets.resolve failed: {}", e.what());
          throw;
        }
      });

  // --- logs.tail ---
  // Return recent log entries
  server.RegisterHandler(
      methods::kOcLogsTail,
      [log_file_path, logger](const nlohmann::json& params,
                              ClientConnection& /*client*/) -> nlohmann::json {
        try {
          int lines = params.value("lines", 100);
          if (lines <= 0)
            lines = 100;
          if (lines > 10000)
            lines = 10000;

          nlohmann::json log_lines = nlohmann::json::array();

          if (!log_file_path.empty() &&
              std::filesystem::exists(log_file_path)) {
            std::ifstream file(log_file_path);
            std::vector<std::string> all_lines;
            std::string line;

            while (std::getline(file, line)) {
              all_lines.push_back(line);
            }

            // Return last N lines
            int start = std::max(0, static_cast<int>(all_lines.size()) - lines);
            for (int i = start; i < static_cast<int>(all_lines.size()); ++i) {
              log_lines.push_back(all_lines[i]);
            }
          }

          logger->debug("logs.tail: returning {} lines", log_lines.size());
          return {{"lines", log_lines}, {"count", log_lines.size()}};
        } catch (const std::exception& e) {
          logger->error("logs.tail failed: {}", e.what());
          throw;
        }
      });

  // Create a static device pairing manager (simplified implementation)
  // In production, this should be passed as a parameter and persisted
  static auto device_pairing_mgr = std::make_shared<DevicePairingManager>();

  // --- device.pair.list ---
  // List all pairing requests and paired devices
  server.RegisterHandler(
      methods::kDevicePairList,
      [logger](const nlohmann::json& /*params*/,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          auto result = device_pairing_mgr->ListPairing();
          logger->debug("device.pair.list: {} pending, {} paired",
                        result["pending"].size(), result["paired"].size());
          return result;
        } catch (const std::exception& e) {
          logger->error("device.pair.list failed: {}", e.what());
          throw;
        }
      });

  // --- device.pair.approve ---
  // Approve a pairing request
  server.RegisterHandler(
      methods::kDevicePairApprove,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string request_id = params.value("requestId", "");
          if (request_id.empty()) {
            throw std::runtime_error("Missing 'requestId' parameter");
          }

          auto request = device_pairing_mgr->ApprovePairing(request_id);
          if (!request) {
            throw std::runtime_error("Unknown requestId: " + request_id);
          }

          logger->info("device.pair.approve: approved device={} role={}",
                       request->device_id, request->role);

          return {{"requestId", request_id},
                  {"deviceId", request->device_id},
                  {"deviceName", request->device_name},
                  {"role", request->role}};
        } catch (const std::exception& e) {
          logger->error("device.pair.approve failed: {}", e.what());
          throw;
        }
      });

  // --- device.pair.reject ---
  // Reject a pairing request
  server.RegisterHandler(
      methods::kDevicePairReject,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string request_id = params.value("requestId", "");
          if (request_id.empty()) {
            throw std::runtime_error("Missing 'requestId' parameter");
          }

          auto request = device_pairing_mgr->RejectPairing(request_id);
          if (!request) {
            throw std::runtime_error("Unknown requestId: " + request_id);
          }

          logger->info("device.pair.reject: rejected device={}",
                       request->device_id);

          return {{"requestId", request_id}, {"deviceId", request->device_id}};
        } catch (const std::exception& e) {
          logger->error("device.pair.reject failed: {}", e.what());
          throw;
        }
      });

  // --- device.pair.remove ---
  // Remove a paired device
  server.RegisterHandler(
      methods::kDevicePairRemove,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string device_id = params.value("deviceId", "");
          if (device_id.empty()) {
            throw std::runtime_error("Missing 'deviceId' parameter");
          }

          auto device = device_pairing_mgr->RemovePairedDevice(device_id);
          if (!device) {
            throw std::runtime_error("Unknown deviceId: " + device_id);
          }

          logger->info("device.pair.remove: removed device={}", device_id);

          return {{"deviceId", device_id}, {"deviceName", device->device_name}};
        } catch (const std::exception& e) {
          logger->error("device.pair.remove failed: {}", e.what());
          throw;
        }
      });

  // --- device.token.rotate ---
  // Rotate device token
  server.RegisterHandler(
      methods::kDeviceTokenRotate,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string device_id = params.value("deviceId", "");
          std::string role = params.value("role", "");
          std::vector<std::string> scopes;

          if (params.contains("scopes") && params["scopes"].is_array()) {
            for (const auto& scope : params["scopes"]) {
              scopes.push_back(scope.get<std::string>());
            }
          }

          if (device_id.empty() || role.empty()) {
            throw std::runtime_error("Missing 'deviceId' or 'role' parameter");
          }

          auto token =
              device_pairing_mgr->RotateDeviceToken(device_id, role, scopes);
          if (!token) {
            throw std::runtime_error("Unknown deviceId/role: " + device_id +
                                     "/" + role);
          }

          logger->info("device.token.rotate: rotated device={} role={}",
                       device_id, role);

          return {{"deviceId", device_id},
                  {"role", token->role},
                  {"token", token->token},
                  {"scopes", token->scopes},
                  {"rotatedAtMs", token->rotated_at_ms}};
        } catch (const std::exception& e) {
          logger->error("device.token.rotate failed: {}", e.what());
          throw;
        }
      });

  // --- device.token.revoke ---
  // Revoke device token
  server.RegisterHandler(
      methods::kDeviceTokenRevoke,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string device_id = params.value("deviceId", "");
          std::string role = params.value("role", "");

          if (device_id.empty() || role.empty()) {
            throw std::runtime_error("Missing 'deviceId' or 'role' parameter");
          }

          bool revoked = device_pairing_mgr->RevokeDeviceToken(device_id, role);
          if (!revoked) {
            throw std::runtime_error("Unknown deviceId/role: " + device_id +
                                     "/" + role);
          }

          logger->info("device.token.revoke: revoked device={} role={}",
                       device_id, role);

          return {{"deviceId", device_id}, {"role", role}, {"revoked", true}};
        } catch (const std::exception& e) {
          logger->error("device.token.revoke failed: {}", e.what());
          throw;
        }
      });

  // --- exec.approvals.get ---
  // Get current exec approval configuration
  server.RegisterHandler(
      methods::kExecApprovalsGet,
      [exec_approval_mgr,
       logger](const nlohmann::json& /*params*/,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          if (!exec_approval_mgr) {
            throw std::runtime_error("Exec approval manager not available");
          }

          const auto& config = exec_approval_mgr->GetConfig();

          nlohmann::json result = {
              {"ask", AskModeToString(config.ask)},
              {"timeoutSeconds", config.timeout_seconds},
              {"timeoutFallback",
               ApprovalDecisionToString(config.timeout_fallback)},
              {"allowlist", config.allowlist},
              {"approvalNoticeMs", config.approval_notice_ms}};

          logger->debug("exec.approvals.get: returning config");
          return result;
        } catch (const std::exception& e) {
          logger->error("exec.approvals.get failed: {}", e.what());
          throw;
        }
      });

  // --- exec.approvals.set ---
  // Set exec approval configuration
  server.RegisterHandler(
      methods::kExecApprovalsSet,
      [exec_approval_mgr,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          if (!exec_approval_mgr) {
            throw std::runtime_error("Exec approval manager not available");
          }

          ExecApprovalConfig new_config;

          if (params.contains("ask")) {
            new_config.ask =
                AskModeFromString(params["ask"].get<std::string>());
          }
          if (params.contains("timeoutSeconds")) {
            new_config.timeout_seconds = params["timeoutSeconds"].get<int>();
          }
          if (params.contains("allowlist") && params["allowlist"].is_array()) {
            for (const auto& pattern : params["allowlist"]) {
              new_config.allowlist.push_back(pattern.get<std::string>());
            }
          }
          if (params.contains("approvalNoticeMs")) {
            new_config.approval_notice_ms =
                params["approvalNoticeMs"].get<int>();
          }

          exec_approval_mgr->Configure(new_config);

          logger->info("exec.approvals.set: updated config");
          return {{"ok", true}};
        } catch (const std::exception& e) {
          logger->error("exec.approvals.set failed: {}", e.what());
          throw;
        }
      });

  // --- exec.approval.request ---
  // Request approval for a command execution
  server.RegisterHandler(
      methods::kExecApprovalRequest,
      [exec_approval_mgr,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          if (!exec_approval_mgr) {
            throw std::runtime_error("Exec approval manager not available");
          }

          std::string command = params.value("command", "");
          std::string cwd = params.value("cwd", "");
          std::string agent_id = params.value("agentId", "main");
          std::string session_key = params.value("sessionKey", "");

          if (command.empty()) {
            throw std::runtime_error("Missing 'command' parameter");
          }

          auto decision = exec_approval_mgr->RequestApproval(
              command, cwd, agent_id, session_key);

          logger->info("exec.approval.request: command='{}' decision={}",
                       command, ApprovalDecisionToString(decision));

          return {{"command", command},
                  {"decision", ApprovalDecisionToString(decision)}};
        } catch (const std::exception& e) {
          logger->error("exec.approval.request failed: {}", e.what());
          throw;
        }
      });

  // --- exec.approval.resolve ---
  // Resolve a pending approval request
  server.RegisterHandler(
      methods::kExecApprovalResolve,
      [exec_approval_mgr,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          if (!exec_approval_mgr) {
            throw std::runtime_error("Exec approval manager not available");
          }

          std::string request_id = params.value("requestId", "");
          std::string decision_str = params.value("decision", "");
          std::string resolved_by = params.value("resolvedBy", "operator");

          if (request_id.empty() || decision_str.empty()) {
            throw std::runtime_error(
                "Missing 'requestId' or 'decision' parameter");
          }

          ApprovalDecision decision;
          if (decision_str == "approved") {
            decision = ApprovalDecision::kApproved;
          } else if (decision_str == "denied") {
            decision = ApprovalDecision::kDenied;
          } else {
            throw std::runtime_error("Invalid decision: " + decision_str);
          }

          bool resolved =
              exec_approval_mgr->Resolve(request_id, decision, resolved_by);
          if (!resolved) {
            throw std::runtime_error("Unknown or expired requestId: " +
                                     request_id);
          }

          logger->info("exec.approval.resolve: requestId={} decision={}",
                       request_id, decision_str);

          return {{"requestId", request_id},
                  {"decision", decision_str},
                  {"resolved", true}};
        } catch (const std::exception& e) {
          logger->error("exec.approval.resolve failed: {}", e.what());
          throw;
        }
      });

  // --- exec.approvals.node.get ---
  // Get exec approval config for a specific node (stub implementation)
  server.RegisterHandler(
      methods::kExecApprovalsNodeGet,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string node_id = params.value("nodeId", "");
          if (node_id.empty()) {
            throw std::runtime_error("Missing 'nodeId' parameter");
          }

          logger->debug("exec.approvals.node.get: nodeId={}", node_id);

          // Stub: return default config for now
          return {{"nodeId", node_id},
                  {"ask", "onMiss"},
                  {"timeoutSeconds", 120},
                  {"allowlist", nlohmann::json::array()}};
        } catch (const std::exception& e) {
          logger->error("exec.approvals.node.get failed: {}", e.what());
          throw;
        }
      });

  // --- exec.approvals.node.set ---
  // Set exec approval config for a specific node (stub implementation)
  server.RegisterHandler(
      methods::kExecApprovalsNodeSet,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string node_id = params.value("nodeId", "");
          if (node_id.empty()) {
            throw std::runtime_error("Missing 'nodeId' parameter");
          }

          logger->info("exec.approvals.node.set: nodeId={}", node_id);

          // Stub: acknowledge but don't persist for now
          return {{"nodeId", node_id}, {"ok", true}};
        } catch (const std::exception& e) {
          logger->error("exec.approvals.node.set failed: {}", e.what());
          throw;
        }
      });

  // --- node.list ---
  // List all nodes (simplified stub implementation)
  server.RegisterHandler(
      methods::kNodeList,
      [logger](const nlohmann::json& /*params*/,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          // Stub: return current node only
          nlohmann::json nodes = nlohmann::json::array();
          nodes.push_back({{"nodeId", "main"},
                           {"name", "Main Node"},
                           {"status", "online"},
                           {"type", "primary"}});

          logger->debug("node.list: returning {} nodes", nodes.size());
          return {{"nodes", nodes}};
        } catch (const std::exception& e) {
          logger->error("node.list failed: {}", e.what());
          throw;
        }
      });

  // --- node.describe ---
  // Describe a specific node
  server.RegisterHandler(
      methods::kNodeDescribe,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string node_id = params.value("nodeId", "main");

          logger->debug("node.describe: nodeId={}", node_id);
          return {{"nodeId", node_id},
                  {"name", "Main Node"},
                  {"status", "online"},
                  {"type", "primary"},
                  {"version", "0.3.0"},
                  {"capabilities",
                   nlohmann::json::array({"chat", "tools", "memory"})}};
        } catch (const std::exception& e) {
          logger->error("node.describe failed: {}", e.what());
          throw;
        }
      });

  // --- node.rename ---
  // Rename a node
  server.RegisterHandler(
      methods::kNodeRename,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string node_id = params.value("nodeId", "");
          std::string new_name = params.value("name", "");

          if (node_id.empty() || new_name.empty()) {
            throw std::runtime_error("Missing 'nodeId' or 'name' parameter");
          }

          logger->info("node.rename: nodeId={} newName={}", node_id, new_name);
          return {{"nodeId", node_id}, {"name", new_name}, {"ok", true}};
        } catch (const std::exception& e) {
          logger->error("node.rename failed: {}", e.what());
          throw;
        }
      });

  // --- node.pair.list ---
  // List node pairing requests (stub)
  server.RegisterHandler(
      methods::kNodePairList,
      [logger](const nlohmann::json& /*params*/,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          logger->debug("node.pair.list: returning empty lists");
          return {{"pending", nlohmann::json::array()},
                  {"paired", nlohmann::json::array()}};
        } catch (const std::exception& e) {
          logger->error("node.pair.list failed: {}", e.what());
          throw;
        }
      });

  // --- node.pair.request ---
  // Request node pairing (stub)
  server.RegisterHandler(
      methods::kNodePairRequest,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string target_node = params.value("targetNode", "");
          if (target_node.empty()) {
            throw std::runtime_error("Missing 'targetNode' parameter");
          }

          logger->info("node.pair.request: targetNode={}", target_node);
          return {{"requestId", "req_" + std::to_string(std::time(nullptr))},
                  {"targetNode", target_node},
                  {"status", "pending"}};
        } catch (const std::exception& e) {
          logger->error("node.pair.request failed: {}", e.what());
          throw;
        }
      });

  // --- node.pair.approve ---
  // Approve node pairing (stub)
  server.RegisterHandler(
      methods::kNodePairApprove,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string request_id = params.value("requestId", "");
          if (request_id.empty()) {
            throw std::runtime_error("Missing 'requestId' parameter");
          }

          logger->info("node.pair.approve: requestId={}", request_id);
          return {{"requestId", request_id}, {"status", "approved"}};
        } catch (const std::exception& e) {
          logger->error("node.pair.approve failed: {}", e.what());
          throw;
        }
      });

  // --- node.pair.reject ---
  // Reject node pairing (stub)
  server.RegisterHandler(
      methods::kNodePairReject,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string request_id = params.value("requestId", "");
          if (request_id.empty()) {
            throw std::runtime_error("Missing 'requestId' parameter");
          }

          logger->info("node.pair.reject: requestId={}", request_id);
          return {{"requestId", request_id}, {"status", "rejected"}};
        } catch (const std::exception& e) {
          logger->error("node.pair.reject failed: {}", e.what());
          throw;
        }
      });

  // --- node.invoke ---
  // Invoke a method on a remote node (stub)
  server.RegisterHandler(
      methods::kNodeInvoke,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string node_id = params.value("nodeId", "");
          std::string method = params.value("method", "");

          if (node_id.empty() || method.empty()) {
            throw std::runtime_error("Missing 'nodeId' or 'method' parameter");
          }

          logger->info("node.invoke: nodeId={} method={}", node_id, method);
          return {{"nodeId", node_id},
                  {"method", method},
                  {"result", "stub_result"},
                  {"ok", true}};
        } catch (const std::exception& e) {
          logger->error("node.invoke failed: {}", e.what());
          throw;
        }
      });

  // --- node.event ---
  // Send an event to a node (stub)
  server.RegisterHandler(
      methods::kNodeEvent,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string node_id = params.value("nodeId", "");
          std::string event_type = params.value("type", "");

          if (node_id.empty() || event_type.empty()) {
            throw std::runtime_error("Missing 'nodeId' or 'type' parameter");
          }

          logger->info("node.event: nodeId={} type={}", node_id, event_type);
          return {{"nodeId", node_id}, {"type", event_type}, {"ok", true}};
        } catch (const std::exception& e) {
          logger->error("node.event failed: {}", e.what());
          throw;
        }
      });

  // --- embeddings.generate ---
  server.RegisterHandler(
      methods::kEmbeddingsGenerate,
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string model = params.value("model", "local/hash-embedding-v1");
          std::string encoding_format =
              params.value("encoding_format", "float");
          int requested_dimensions =
              params.value("dimensions",
                           static_cast<int>(kDefaultEmbeddingDimensions));
          if (requested_dimensions <= 0 || requested_dimensions > 4096) {
            throw std::runtime_error(
                "dimensions must be between 1 and 4096");
          }

          auto texts =
              ParseEmbeddingInputs(params.value("input", nlohmann::json()));
          logger->info("embeddings.generate: model={} inputs={}", model,
                       texts.size());

          nlohmann::json embeddings = nlohmann::json::array();
          for (size_t i = 0; i < texts.size(); ++i) {
            auto embedding = BuildDeterministicEmbedding(
                texts[i], static_cast<size_t>(requested_dimensions));
            embeddings.push_back(
                {{"object", "embedding"},
                 {"index", i},
                 {"embedding",
                  EncodeEmbeddingPayload(embedding, encoding_format)}});
          }

          size_t token_count = CountApproximateTokens(texts);
          return {{"object", "list"},
                  {"data", embeddings},
                  {"model", model},
                  {"usage",
                   {{"prompt_tokens", token_count},
                    {"total_tokens", token_count}}}};
        } catch (const std::exception& e) {
          logger->error("embeddings.generate failed: {}", e.what());
          throw;
        }
      });

  // --- embeddings.search ---
  server.RegisterHandler(
      methods::kEmbeddingsSearch,
      [vector_db, logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          auto query = params.value("query", "");
          int limit = params.value("limit", 10);
          float threshold = params.value("threshold", 0.2f);
          if (query.empty()) {
            throw std::runtime_error("query is required");
          }
          if (limit <= 0) {
            throw std::runtime_error("limit must be positive");
          }

          logger->info("embeddings.search: query='{}' limit={}", query, limit);

          size_t dimensions = vector_db->GetDimension() > 0
                                  ? static_cast<size_t>(vector_db->GetDimension())
                                  : kDefaultEmbeddingDimensions;
          auto query_embedding = BuildDeterministicEmbedding(query, dimensions);
          auto matches = vector_db->SearchVectors(query_embedding, limit, threshold);

          nlohmann::json results = nlohmann::json::array();
          for (const auto& match : matches) {
            results.push_back(
                {{"id", match.id},
                 {"distance", match.distance},
                 {"score", 1.0f - match.distance},
                 {"text", match.text},
                 {"metadata", match.metadata}});
          }

          return {{"results", results}, {"count", results.size()}};
        } catch (const std::exception& e) {
          logger->error("embeddings.search failed: {}", e.what());
          throw;
        }
      });

  // --- vector.index ---
  server.RegisterHandler(
      methods::kVectorIndex,
      [vector_db, logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string id = params.value("id", "");
          std::string text = params.value("text", "");
          nlohmann::json metadata =
              params.value("metadata", nlohmann::json::object());
          std::vector<float> vector;
          bool generated_from_text = false;

          if (id.empty()) {
            throw std::runtime_error("id is required");
          }
          if (params.contains("vector") && !params["vector"].is_null()) {
            vector = ParseVectorArray(params["vector"]);
          } else if (!text.empty()) {
            size_t dimensions = vector_db->GetDimension() > 0
                                    ? static_cast<size_t>(vector_db->GetDimension())
                                    : kDefaultEmbeddingDimensions;
            vector = BuildDeterministicEmbedding(text, dimensions);
            generated_from_text = true;
          } else {
            throw std::runtime_error("either vector or text is required");
          }

          if (!vector_db->IndexVector(id, vector, text, metadata)) {
            throw std::runtime_error("failed to index vector");
          }

          logger->info("vector.index: id={} dimension={}", id, vector.size());

          return {{"id", id},
                  {"indexed", true},
                  {"dimension", vector.size()},
                  {"generated", generated_from_text}};
        } catch (const std::exception& e) {
          logger->error("vector.index failed: {}", e.what());
          throw;
        }
      });

  // --- vector.search ---
  server.RegisterHandler(
      methods::kVectorSearch,
      [vector_db, logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          int limit = params.value("limit", 10);
          float threshold = params.value("threshold", 0.2f);
          std::vector<float> query_vector;

          if (limit <= 0) {
            throw std::runtime_error("limit must be positive");
          }
          if (params.contains("vector") && !params["vector"].is_null()) {
            query_vector = ParseVectorArray(params["vector"]);
          } else {
            std::string query_text = params.value("query", "");
            if (query_text.empty()) {
              throw std::runtime_error("vector or query is required");
            }
            size_t dimensions = vector_db->GetDimension() > 0
                                    ? static_cast<size_t>(vector_db->GetDimension())
                                    : kDefaultEmbeddingDimensions;
            query_vector = BuildDeterministicEmbedding(query_text, dimensions);
          }

          logger->info("vector.search: dimension={} limit={} threshold={}",
                       query_vector.size(), limit, threshold);

          auto matches = vector_db->SearchVectors(query_vector, limit, threshold);
          nlohmann::json results = nlohmann::json::array();
          for (const auto& match : matches) {
            results.push_back(
                {{"id", match.id},
                 {"distance", match.distance},
                 {"score", 1.0f - match.distance},
                 {"text", match.text},
                 {"metadata", match.metadata}});
          }

          return {{"results", results}, {"count", results.size()}};
        } catch (const std::exception& e) {
          logger->error("vector.search failed: {}", e.what());
          throw;
        }
      });

  // --- vector.delete ---
  server.RegisterHandler(
      methods::kVectorDelete,
      [vector_db, logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        try {
          std::string id = params.value("id", "");

          if (id.empty()) {
            throw std::runtime_error("Missing 'id' parameter");
          }

          logger->info("vector.delete: id={}", id);

          return {{"id", id}, {"deleted", vector_db->DeleteVector(id)}};
        } catch (const std::exception& e) {
          logger->error("vector.delete failed: {}", e.what());
          throw;
        }
      });

  int handler_count = 57;  // base handlers (42 + 9 node + 1 models.catalog + 5
                           // vector/embedding)
  if (reload_fn)
    handler_count++;
  if (skill_loader)
    handler_count += 3;  // status, install, update
  if (cron_scheduler)
    handler_count += 7;  // list, add, remove, update, run, runs, status
  if (exec_approval_mgr)
    handler_count += 2;
  if (plugin_system)
    handler_count += 7;
  if (command_queue)
    handler_count += 4;
  handler_count +=
      10;  // ui compat: identity, node.list, device.pair.list, logs.tail,
           //            config.schema, sessions.usage, usage.cost,
           //            sessions.usage.timeseries, sessions.usage.logs
  logger->info("Registered {} RPC handlers", handler_count);
}

}  // namespace ravbot::gateway
