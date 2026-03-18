// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/core/agent_loop.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <codecvt>
#include <locale>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "ravbot/core/context_pruner.hpp"
#include "ravbot/core/embedding_manager.hpp"
#include "ravbot/core/memory_manager.hpp"
#include "ravbot/core/session_compaction.hpp"
#include "ravbot/core/skill_loader.hpp"
#include "ravbot/gateway/protocol.hpp"
#include "ravbot/providers/failover_resolver.hpp"
#include "ravbot/providers/provider_error.hpp"
#include "ravbot/providers/provider_registry.hpp"
#include "ravbot/tools/tool_registry.hpp"

// Bring event name constants into scope
namespace events = ravbot::gateway::events;

namespace ravbot {

// Estimate token count for a message list (rough: 4 chars ≈ 1 token)
static int estimate_tokens(const std::vector<Message>& messages) {
  int chars = 0;
  for (const auto& msg : messages) {
    chars += static_cast<int>(msg.role.size());
    for (const auto& block : msg.content) {
      chars += static_cast<int>(block.text.size());
      chars += static_cast<int>(block.content.size());
      if (!block.input.is_null())
        chars += static_cast<int>(block.input.dump().size());
    }
  }
  return chars / 4;
}

static int estimate_tool_tokens(const std::vector<nlohmann::json>& tools) {
  int chars = 0;
  for (const auto& tool : tools) {
    chars += static_cast<int>(tool.dump().size());
  }
  return chars / 4;
}

static std::vector<nlohmann::json>
build_request_tools(const std::shared_ptr<ToolRegistry>& tool_registry) {
  nlohmann::json tools_json = nlohmann::json::array();
  auto schemas = tool_registry->GetToolSchemas();

  spdlog::info("build_request_tools: Got {} tool schemas from registry", schemas.size());

  for (const auto& schema : schemas) {
    nlohmann::json tool;
    tool["type"] = "function";
    tool["function"]["name"] = schema.name;
    tool["function"]["description"] = schema.description;
    tool["function"]["parameters"] = schema.parameters;
    tools_json.push_back(tool);
  }

  auto result = tools_json.get<std::vector<nlohmann::json>>();
  spdlog::info("build_request_tools: Returning {} tools", result.size());
  return result;
}

static std::vector<Message> compress_history_for_context_budget(
    const std::vector<Message>& history, const std::string& system_prompt,
    const std::string& latest_user_message,
    const std::vector<nlohmann::json>& tools, int context_window,
    std::shared_ptr<ContextPruner> context_pruner,
    const std::shared_ptr<spdlog::logger>& logger) {
  if (!context_pruner || history.empty() || context_window <= 0) {
    return history;
  }

  constexpr double kCompressionThresholdRatio = 0.9;

  int fixed_tokens = estimate_tool_tokens(tools);
  if (!system_prompt.empty()) {
    fixed_tokens += ContextPruner::EstimateTokens(
        std::vector<Message>{Message{"system", system_prompt}});
  }
  fixed_tokens += ContextPruner::EstimateTokens(
      std::vector<Message>{Message{"user", latest_user_message}});

  const int threshold_tokens =
      static_cast<int>(context_window * kCompressionThresholdRatio);
  const int history_tokens = ContextPruner::EstimateTokens(history);
  const int total_estimated = fixed_tokens + history_tokens;
  if (total_estimated <= threshold_tokens) {
    return history;
  }

  const int target_history_tokens =
      std::max(0, threshold_tokens - fixed_tokens);
  CompressionStats stats;
  auto compressed =
      ContextPruner::CompressWithStats(history, target_history_tokens, stats);

  if (logger) {
    logger->info(
        "Context pruner triggered at 90% threshold: total={} threshold={} "
        "history={} target_history={} removed={} saved={}",
        total_estimated, threshold_tokens, history_tokens,
        target_history_tokens, stats.messages_removed, stats.tokens_saved);
  }
  return compressed;
}

static bool has_non_tool_result_blocks(const Message& msg);

struct SalientTokenStats {
  std::unordered_map<std::string, int> weights;
  std::unordered_set<std::string> anchor_tokens;
  int total_weight = 0;
};

static std::u32string utf8_to_u32(const std::string& text) {
  try {
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
    return converter.from_bytes(text);
  } catch (const std::range_error&) {
    return {};
  }
}

static std::string u32_to_utf8(const std::u32string& text) {
  std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
  return converter.to_bytes(text);
}

static bool is_cjk_codepoint(char32_t cp) {
  return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
         (cp >= 0xF900 && cp <= 0xFAFF);
}

static bool is_ascii_token_char(char32_t cp) {
  if (cp > 0x7F) {
    return false;
  }
  const auto ch = static_cast<unsigned char>(cp);
  return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '/' ||
         ch == ':' || ch == '#';
}

static bool is_ascii_text(const std::string& text) {
  return std::all_of(text.begin(), text.end(),
                     [](unsigned char ch) { return ch <= 0x7F; });
}

static bool is_latin_stopword(const std::string& token) {
  static const std::unordered_set<std::string> kStopwords = {
      "a",    "an",     "and",  "are",    "be",   "but",  "can",  "check",
      "find", "for",    "from", "help",   "how",  "i",    "in",   "into",
      "is",   "it",     "look", "me",     "more", "need", "of",   "on",
      "or",   "please", "read", "search", "show", "tell", "that", "the",
      "this", "to",     "up",   "want",   "what", "with", "you"};
  return kStopwords.count(token) > 0;
}

static bool is_generic_cjk_token(const std::u32string& token) {
  if (token.empty()) {
    return true;
  }

  static const std::unordered_set<std::string> kStopTokens = {
      u8"一个", u8"一下", u8"什么", u8"今天", u8"可以",
      u8"帮我", u8"帮忙", u8"现在", u8"相关", u8"需要"};
  const auto utf8 = u32_to_utf8(token);
  if (kStopTokens.count(utf8) > 0) {
    return true;
  }

  static const std::unordered_set<char32_t> kStopChars = {
      U'的', U'了', U'吗', U'呢', U'吧', U'啊', U'我', U'你', U'他',
      U'她', U'它', U'们', U'是', U'在', U'有', U'和', U'与', U'及',
      U'并', U'或', U'就', U'都', U'很', U'太', U'也', U'再', U'还',
      U'把', U'被', U'让', U'给', U'从', U'向', U'到', U'将', U'会',
      U'能', U'可', U'要', U'想', U'来', U'去', U'说', U'问', U'做'};
  for (char32_t cp : token) {
    if (kStopChars.count(cp) > 0) {
      return true;
    }
  }
  return false;
}

static void add_salient_token(SalientTokenStats& stats,
                              const std::string& token, int weight,
                              bool anchor) {
  if (token.empty() || weight <= 0) {
    return;
  }
  if (stats.weights.emplace(token, weight).second) {
    stats.total_weight += weight;
  }
  if (anchor) {
    stats.anchor_tokens.insert(token);
  }
}

static SalientTokenStats extract_salient_tokens(const std::string& text) {
  SalientTokenStats stats;
  const auto codepoints = utf8_to_u32(text);

  std::string ascii_run;
  std::u32string cjk_run;

  auto flush_ascii = [&]() {
    if (ascii_run.empty()) {
      return;
    }
    std::string token;
    token.reserve(ascii_run.size());
    for (char ch : ascii_run) {
      token.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    const bool has_symbol =
        token.find_first_of("0123456789/._-:#") != std::string::npos;
    if ((token.size() >= 3 || has_symbol) && !is_latin_stopword(token)) {
      add_salient_token(stats, token, has_symbol || token.size() >= 5 ? 3 : 2,
                        has_symbol || token.size() >= 5);
    }
    ascii_run.clear();
  };

  auto flush_cjk = [&]() {
    if (cjk_run.size() < 2) {
      cjk_run.clear();
      return;
    }

    const size_t max_window = std::min<size_t>(3, cjk_run.size());
    for (size_t window = 2; window <= max_window; ++window) {
      for (size_t i = 0; i + window <= cjk_run.size(); ++i) {
        const auto token_u32 = cjk_run.substr(i, window);
        if (is_generic_cjk_token(token_u32)) {
          continue;
        }
        add_salient_token(stats, u32_to_utf8(token_u32), window >= 3 ? 2 : 1,
                          window >= 3);
      }
    }

    cjk_run.clear();
  };

  for (char32_t cp : codepoints) {
    if (is_ascii_token_char(cp)) {
      flush_cjk();
      ascii_run.push_back(static_cast<char>(cp));
      continue;
    }
    if (is_cjk_codepoint(cp)) {
      flush_ascii();
      cjk_run.push_back(cp);
      continue;
    }
    flush_ascii();
    flush_cjk();
  }

  flush_ascii();
  flush_cjk();
  return stats;
}

static bool looks_like_follow_up_message(const std::string& text) {
  std::string lower = text;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  static const std::vector<std::string> kMarkers = {
      "based on that", "continue",     "elaborate",  "expand on",  "go on",
      "more detail",   "tell me more", "what else",  u8"上面",     u8"前面",
      u8"刚才",        u8"再详细",     u8"展开",     u8"接着",     u8"接着说",
      u8"继续",        u8"继续说",     u8"详细一点", u8"详细一些", u8"补充",
      u8"进一步"};

  return std::any_of(kMarkers.begin(), kMarkers.end(),
                     [&](const std::string& marker) {
                       return lower.find(marker) != std::string::npos;
                     });
}

static bool is_brief_continuation_prompt(const std::string& text) {
  auto trimmed = text;
  const auto is_ascii_space = [](unsigned char ch) {
    return std::isspace(ch) != 0;
  };

  while (!trimmed.empty() &&
         is_ascii_space(static_cast<unsigned char>(trimmed.front()))) {
    trimmed.erase(trimmed.begin());
  }
  while (!trimmed.empty() &&
         is_ascii_space(static_cast<unsigned char>(trimmed.back()))) {
    trimmed.pop_back();
  }

  while (!trimmed.empty()) {
    const unsigned char tail = static_cast<unsigned char>(trimmed.back());
    if (tail == '.' || tail == '!' || tail == '?' || tail == ',' ||
        tail == ';' || tail == ':' || tail == '~') {
      trimmed.pop_back();
      continue;
    }
    break;
  }

  if (trimmed.empty() || trimmed.size() > 32) {
    return false;
  }

  std::string lower = trimmed;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  static const std::unordered_set<std::string> kExactMarkers = {
      "continue",       "please continue", "continue please", "go on",
      "please go on",   "keep going",      "carry on",        u8"继续",
      u8"请继续",       u8"继续吧",        u8"继续呀",        u8"继续说",
      u8"接着",         u8"接着说",        u8"接着来",        u8"继续下去"};
  return kExactMarkers.count(lower) > 0;
}

static bool has_lookup_intent(const std::string& text) {
  std::string lower = text;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  static const std::vector<std::string> kAsciiMarkers = {
      "search",   "look up", "lookup",        "find ",        "find on",
      "research", "browse",  "github search", "search github"};
  for (const auto& marker : kAsciiMarkers) {
    if (lower.find(marker) != std::string::npos) {
      return true;
    }
  }

  static const std::vector<std::string> kCjkMarkers = {
      u8"搜索",   u8"查找", u8"帮我找",   u8"帮我搜", u8"查一下",
      u8"搜一下", u8"检索", u8"研究一下", u8"查查",   u8"找一下"};
  for (const auto& marker : kCjkMarkers) {
    if (text.find(marker) != std::string::npos) {
      return true;
    }
  }

  return false;
}

static bool
response_looks_like_unfulfilled_lookup_preamble(const std::string& text) {
  if (text.empty()) {
    return false;
  }

  std::string lower = text;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  const bool has_url = lower.find("http://") != std::string::npos ||
                       lower.find("https://") != std::string::npos ||
                       lower.find("github.com") != std::string::npos;
  const bool has_list = text.find("\n1.") != std::string::npos ||
                        text.find("\n- ") != std::string::npos ||
                        text.find("\n•") != std::string::npos;
  if (has_url || has_list) {
    return false;
  }

  static const std::vector<std::string> kAsciiMarkers = {
      "let me search",          "i'll search",
      "i will search",          "let me look that up",
      "i'll look that up",      "based on the search results",
      "let me get more detail", "let me gather more detail"};
  for (const auto& marker : kAsciiMarkers) {
    if (lower.find(marker) != std::string::npos) {
      return true;
    }
  }

  static const std::vector<std::string> kCjkMarkers = {
      u8"根据搜索结果", u8"让我为你获取更详细的信息",
      u8"让我帮你查找", u8"让我帮你搜索",
      u8"我来帮你搜索", u8"我来帮你查找",
      u8"让我继续查",   u8"让我获取更详细的信息",
      u8"我来搜索",     u8"让我搜索",
      u8"我将搜索",     u8"让我通过"};
  for (const auto& marker : kCjkMarkers) {
    if (text.find(marker) != std::string::npos) {
      return true;
    }
  }

  return false;
}

static bool response_contains_negative_conclusion(const std::string& text) {
  static const std::vector<std::string> kNegativeMarkers = {
      u8"没有找到", u8"没找到", u8"未找到", u8"不存在",
      u8"没有专门", u8"没有集中", u8"还没有形成", u8"没有公开",
      u8"目前没有", u8"暂时没有",
      "not found", "no results", "doesn't exist", "not available",
      "no public", "currently no"
  };

  for (const auto& marker : kNegativeMarkers) {
    if (text.find(marker) != std::string::npos) {
      return true;
    }
  }
  return false;
}

static int count_substring_occurrences(const std::string& text,
                                       const std::string& needle) {
  if (needle.empty()) {
    return 0;
  }

  int count = 0;
  size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

static bool
response_looks_like_raw_search_results_dump(const std::string& text) {
  if (text.empty()) {
    return false;
  }

  std::string lower = text;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  const bool has_preface =
      lower.find("here are the search results for") != std::string::npos ||
      lower.find("search results for") != std::string::npos ||
      text.find(u8"以下是搜索结果") != std::string::npos ||
      text.find(u8"以下是 \"") != std::string::npos;
  const bool has_disclaimer =
      lower.find("may not be fully accurate or up-to-date") !=
          std::string::npos ||
      text.find(u8"可能不完全准确") != std::string::npos;
  const bool has_numbered_list = text.find("\n1.") != std::string::npos ||
                                 text.find("\n1. **") != std::string::npos;
  const int source_count = count_substring_occurrences(text, "Source: ") +
                           count_substring_occurrences(text, u8"来源：");

  return (has_preface && has_numbered_list && source_count >= 2) ||
         (has_disclaimer && source_count >= 2);
}

static std::string collect_message_text(const Message& msg) {
  std::string text = msg.text();
  for (const auto& block : msg.content) {
    if (block.type == "tool_result" && !block.content.empty()) {
      if (!text.empty()) {
        text += "\n";
      }
      text += block.content.substr(0, 200);
    }
  }
  return text;
}

static std::string collect_turn_text(const std::vector<Message>& history,
                                     size_t turn_start) {
  std::string combined;
  for (size_t i = turn_start; i < history.size(); ++i) {
    const auto text = collect_message_text(history[i]);
    if (text.empty()) {
      continue;
    }
    if (!combined.empty()) {
      combined += "\n";
    }
    combined += text;
  }
  return combined;
}

static bool
latest_message_relates_to_recent_turn(const std::string& latest_user_text,
                                      const std::string& recent_turn_text) {
  const auto latest_tokens = extract_salient_tokens(latest_user_text);
  const auto recent_tokens = extract_salient_tokens(recent_turn_text);
  if (latest_tokens.total_weight <= 0 || recent_tokens.weights.empty()) {
    return false;
  }

  std::string recent_lower = recent_turn_text;
  std::transform(
      recent_lower.begin(), recent_lower.end(), recent_lower.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  bool has_ascii_anchor = false;
  bool matched_ascii_anchor = false;
  for (const auto& token : latest_tokens.anchor_tokens) {
    if (!is_ascii_text(token)) {
      continue;
    }
    has_ascii_anchor = true;
    if (recent_lower.find(token) != std::string::npos) {
      matched_ascii_anchor = true;
      break;
    }
  }
  if (has_ascii_anchor && !matched_ascii_anchor) {
    return false;
  }

  int matched_weight = 0;
  bool matched_anchor = latest_tokens.anchor_tokens.empty();
  for (const auto& [token, weight] : latest_tokens.weights) {
    if (recent_tokens.weights.count(token) == 0) {
      continue;
    }
    matched_weight += weight;
    if (latest_tokens.anchor_tokens.count(token) > 0) {
      matched_anchor = true;
    }
  }

  if (!matched_anchor || matched_weight <= 0) {
    return false;
  }

  const double overlap_ratio =
      static_cast<double>(matched_weight) / latest_tokens.total_weight;
  return overlap_ratio >= 0.25;
}

static std::vector<Message>
focus_history_on_latest_turn(const std::vector<Message>& history,
                             const std::string& latest_user_text,
                             const std::shared_ptr<spdlog::logger>& logger) {
  if (history.empty()) {
    return history;
  }

  size_t last_user_turn = history.size();
  for (size_t i = history.size(); i-- > 0;) {
    if (has_non_tool_result_blocks(history[i])) {
      last_user_turn = i;
      break;
    }
  }

  if (last_user_turn >= history.size()) {
    return history;
  }

  const auto recent_turn_text = collect_turn_text(history, last_user_turn);
  const bool keep_recent_turn =
      looks_like_follow_up_message(latest_user_text) ||
      latest_message_relates_to_recent_turn(latest_user_text, recent_turn_text);

  if (!keep_recent_turn) {
    if (logger) {
      logger->info(
          "Context focus: dropped {} stale history messages for new user turn",
          history.size());
    }
    return {};
  }

  size_t slice_start = last_user_turn;
  if (slice_start == history.size() - 1 && slice_start > 0 &&
      history[slice_start].role == "user" &&
      history[slice_start - 1].role == "assistant") {
    --slice_start;
  }

  if (slice_start == 0) {
    return history;
  }

  std::vector<Message> focused(history.begin() + slice_start, history.end());
  if (logger) {
    logger->info(
        "Context focus: narrowed history from {} to {} messages around recent "
        "turn",
        history.size(), focused.size());
  }
  return focused;
}

static bool should_retry_provider_error(const ProviderError& error) {
  switch (error.Kind()) {
    case ProviderErrorKind::kRateLimit:
    case ProviderErrorKind::kTransient:
    case ProviderErrorKind::kTimeout:
      return true;
    default:
      return false;
  }
}

static bool
is_anthropic_provider(const std::shared_ptr<LLMProvider>& provider) {
  return provider && provider->GetProviderName() == "anthropic";
}

static std::unordered_set<std::string>
collect_tool_use_ids(const Message& msg) {
  std::unordered_set<std::string> ids;
  for (const auto& block : msg.content) {
    if (block.type == "tool_use" && !block.id.empty()) {
      ids.insert(block.id);
    }
  }
  return ids;
}

static std::vector<Message>
repair_tool_message_pairing(const std::vector<Message>& messages) {
  std::vector<Message> repaired;
  repaired.reserve(messages.size());

  for (size_t i = 0; i < messages.size(); ++i) {
    const auto& msg = messages[i];

    if (msg.role != "assistant") {
      if (msg.role == "user") {
        Message filtered_user;
        filtered_user.role = msg.role;
        for (const auto& block : msg.content) {
          if (block.type != "tool_result") {
            filtered_user.content.push_back(block);
          }
        }
        if (!filtered_user.content.empty()) {
          repaired.push_back(std::move(filtered_user));
        }
      } else {
        repaired.push_back(msg);
      }
      continue;
    }

    auto tool_use_ids = collect_tool_use_ids(msg);
    if (tool_use_ids.empty()) {
      repaired.push_back(msg);
      continue;
    }

    std::unordered_set<std::string> matched_tool_ids;
    Message filtered_next_user;
    bool consumed_next_user = false;

    if (i + 1 < messages.size() && messages[i + 1].role == "user") {
      const auto& next_user = messages[i + 1];
      filtered_next_user.role = next_user.role;

      for (const auto& block : next_user.content) {
        if (block.type == "tool_result") {
          if (!block.tool_use_id.empty() &&
              tool_use_ids.count(block.tool_use_id) > 0) {
            matched_tool_ids.insert(block.tool_use_id);
            filtered_next_user.content.push_back(block);
          }
        } else {
          filtered_next_user.content.push_back(block);
        }
      }

      consumed_next_user = true;
    }

    Message filtered_assistant;
    filtered_assistant.role = msg.role;
    for (const auto& block : msg.content) {
      if (block.type != "tool_use") {
        filtered_assistant.content.push_back(block);
        continue;
      }
      if (matched_tool_ids.count(block.id) > 0) {
        filtered_assistant.content.push_back(block);
      }
    }

    if (filtered_assistant.content.empty() && !msg.content.empty()) {
      filtered_assistant.content.push_back(
          ContentBlock::MakeText("[tool calls omitted]"));
    }
    repaired.push_back(std::move(filtered_assistant));

    if (consumed_next_user) {
      if (!filtered_next_user.content.empty()) {
        repaired.push_back(std::move(filtered_next_user));
      }
      ++i;
    }
  }

  return repaired;
}

static std::vector<Message>
merge_consecutive_user_messages(const std::vector<Message>& messages) {
  std::vector<Message> merged;
  merged.reserve(messages.size());

  for (const auto& msg : messages) {
    if (!merged.empty() && msg.role == "user" && merged.back().role == "user") {
      merged.back().content.insert(merged.back().content.end(),
                                   msg.content.begin(), msg.content.end());
      continue;
    }
    merged.push_back(msg);
  }

  return merged;
}

static bool has_tool_use_blocks(const Message& msg) {
  for (const auto& block : msg.content) {
    if (block.type == "tool_use") {
      return true;
    }
  }
  return false;
}

static bool has_only_tool_result_blocks(const Message& msg) {
  if (msg.role != "user" || msg.content.empty()) {
    return false;
  }
  for (const auto& block : msg.content) {
    if (block.type != "tool_result") {
      return false;
    }
  }
  return true;
}

static bool has_non_tool_result_blocks(const Message& msg) {
  if (msg.role != "user") {
    return false;
  }
  for (const auto& block : msg.content) {
    if (block.type != "tool_result") {
      return true;
    }
  }
  return false;
}

static std::vector<Message>
trim_anthropic_replay_to_current_turn(const std::vector<Message>& messages) {
  if (messages.empty()) {
    return messages;
  }

  // Only trim if the last message is a tool_result (tool replay scenario)
  if (!has_only_tool_result_blocks(messages.back())) {
    return messages;
  }

  size_t first_non_system = 0;
  while (first_non_system < messages.size() &&
         messages[first_non_system].role == "system") {
    ++first_non_system;
  }

  // Find the start of the current turn by looking backwards for the last user
  // message with non-tool_result content (i.e., the user's actual question)
  size_t turn_start = messages.size();
  for (size_t i = messages.size(); i-- > first_non_system;) {
    if (has_non_tool_result_blocks(messages[i])) {
      turn_start = i;
      break;
    }
  }

  if (turn_start >= messages.size()) {
    return messages;
  }

  std::vector<Message> trimmed;
  trimmed.reserve(first_non_system + (messages.size() - turn_start));
  trimmed.insert(trimmed.end(), messages.begin(),
                 messages.begin() + first_non_system);
  trimmed.insert(trimmed.end(), messages.begin() + turn_start, messages.end());
  return trimmed;
}

static std::vector<Message>
collapse_completed_anthropic_tool_turns(const std::vector<Message>& messages) {
  std::vector<Message> collapsed;
  collapsed.reserve(messages.size());

  for (size_t i = 0; i < messages.size(); ++i) {
    const auto& msg = messages[i];

    const bool can_collapse =
        i + 3 < messages.size() && msg.role == "assistant" &&
        has_tool_use_blocks(msg) &&
        has_only_tool_result_blocks(messages[i + 1]) &&
        messages[i + 2].role == "assistant" &&
        !has_tool_use_blocks(messages[i + 2]) && messages[i + 3].role == "user";

    if (!can_collapse) {
      collapsed.push_back(msg);
      continue;
    }

    collapsed.push_back(messages[i + 2]);
    i += 2;
  }

  return collapsed;
}

static std::string extract_tool_schema_name(const nlohmann::json& tool_schema) {
  if (tool_schema.contains("function") && tool_schema["function"].is_object() &&
      tool_schema["function"].contains("name") &&
      tool_schema["function"]["name"].is_string()) {
    return tool_schema["function"]["name"].get<std::string>();
  }
  if (tool_schema.contains("name") && tool_schema["name"].is_string()) {
    return tool_schema["name"].get<std::string>();
  }
  return "";
}

static std::unordered_set<std::string>
collect_trailing_replay_tool_names(const std::vector<Message>& messages) {
  std::unordered_set<std::string> tool_result_ids;
  if (messages.size() < 2) {
    return {};
  }

  const auto& last_msg = messages.back();
  if (last_msg.role != "user") {
    return {};
  }

  for (const auto& block : last_msg.content) {
    if (block.type == "tool_result" && !block.tool_use_id.empty()) {
      tool_result_ids.insert(block.tool_use_id);
    }
  }
  if (tool_result_ids.empty()) {
    return {};
  }

  for (auto it = messages.rbegin() + 1; it != messages.rend(); ++it) {
    if (it->role != "assistant") {
      continue;
    }

    std::unordered_set<std::string> matched_tool_names;
    for (const auto& block : it->content) {
      if (block.type == "tool_use" && !block.id.empty() &&
          tool_result_ids.count(block.id) > 0 && !block.name.empty()) {
        matched_tool_names.insert(block.name);
      }
    }
    if (!matched_tool_names.empty()) {
      return matched_tool_names;
    }
  }

  return {};
}

static void maybe_add_web_search_replay_instruction(
    ChatCompletionRequest& request,
    const std::unordered_set<std::string>& matched_tool_names) {
  if (matched_tool_names.count("web_search") == 0) {
    return;
  }

  static const std::string kReplayHint =
      "[web_search replay hint] Use the existing web_search tool results "
      "already present in this conversation. Do not run another web search "
      "or output a raw search-results dump. Synthesize the provided results "
      "into a concise answer that focuses on the most relevant matches for "
      "the user's request.";

  for (const auto& msg : request.messages) {
    if (msg.role == "system" &&
        msg.text().find(kReplayHint) != std::string::npos) {
      return;
    }
  }

  size_t insert_pos = 0;
  while (insert_pos < request.messages.size() &&
         request.messages[insert_pos].role == "system") {
    ++insert_pos;
  }
  request.messages.insert(request.messages.begin() + insert_pos,
                          Message{"system", kReplayHint});
}

static void narrow_anthropic_tools_for_replay(
    ChatCompletionRequest& request,
    const std::unordered_set<std::string>& matched_tool_names) {
  // DISABLED: This function causes "Improperly formed request" errors
  // when it narrows the tool list but the system prompt still lists all tools.
  // The mismatch between system prompt (21 tools) and actual tools (1 tool)
  // causes Anthropic API to reject the request with HTTP 502.
  //
  // Root cause: When web_search is detected in history, this function filters
  // tools to only include previously-used tools (e.g., github_get_repo).
  // But the system prompt generated by build_system_prompt() still lists all
  // 21 available tools, creating an inconsistency.
  //
  // Solution: Keep all tools in the request to match the system prompt.
  // The web_search replay hint is sufficient to guide the LLM behavior.
  return;

  /* ORIGINAL CODE - DISABLED
  if (matched_tool_names.empty()) {
    return;
  }

  std::vector<nlohmann::json> filtered_tools;
  filtered_tools.reserve(request.tools.size());
  for (const auto& tool : request.tools) {
    const auto tool_name = extract_tool_schema_name(tool);
    if (!tool_name.empty() && matched_tool_names.count(tool_name) > 0) {
      filtered_tools.push_back(tool);
    }
  }

  if (!filtered_tools.empty()) {
    request.tools = std::move(filtered_tools);
  }
  */
}

static void sanitize_request_messages_for_provider(
    const std::shared_ptr<LLMProvider>& provider,
    ChatCompletionRequest& request,
    const std::shared_ptr<TurnValidator>& turn_validator,
    const std::shared_ptr<spdlog::logger>& logger) {
  size_t before_count = request.messages.size();

  request.messages = repair_tool_message_pairing(request.messages);

  // Anthropic-specific sanitization
  if (is_anthropic_provider(provider)) {
    request.messages =
        collapse_completed_anthropic_tool_turns(request.messages);
    request.messages = merge_consecutive_user_messages(request.messages);
    // Apply context trimming for tool replay scenarios
    request.messages = trim_anthropic_replay_to_current_turn(request.messages);
    const auto matched_tool_names =
        collect_trailing_replay_tool_names(request.messages);
    maybe_add_web_search_replay_instruction(request, matched_tool_names);
    narrow_anthropic_tools_for_replay(request, matched_tool_names);
  }

  // Apply turn validation if validator is available
  if (turn_validator && provider) {
    request.messages = turn_validator->ValidateAndFix(
        request.messages, provider->GetProviderName());
  }

  size_t after_count = request.messages.size();

  // Debug logging - always log for debugging purposes
  if (logger) {
    if (before_count != after_count) {
      logger->info("Context trimmed: {} -> {} messages (provider: {})",
                   before_count, after_count,
                   provider ? provider->GetProviderName() : "unknown");
    }
    // Log the actual messages being sent
    logger->debug("Sending {} messages to LLM:", request.messages.size());
    for (size_t i = 0; i < request.messages.size(); ++i) {
      const auto& msg = request.messages[i];
      std::string content_preview;
      if (!msg.content.empty()) {
        if (msg.content[0].type == "text") {
          content_preview = msg.content[0].text.substr(0, 100);
        } else if (msg.content[0].type == "tool_result") {
          content_preview = "[tool_result]";
        } else if (msg.content[0].type == "tool_use") {
          content_preview = "[tool_use: " + msg.content[0].name + "]";
        }
      }
      logger->debug("  Message[{}] role={} content={}", i, msg.role,
                    content_preview);
    }
  }
}

// Truncate a tool result if it exceeds the limit (head + tail with ellipsis)
static std::string truncate_tool_result(const std::string& result,
                                        int max_chars, int keep_lines) {
  if (static_cast<int>(result.size()) <= max_chars)
    return result;

  // Split into lines
  std::vector<std::string> lines;
  std::istringstream stream(result);
  std::string line;
  while (std::getline(stream, line))
    lines.push_back(line);

  if (static_cast<int>(lines.size()) <= keep_lines * 2)
    return result;

  std::string truncated;
  for (int i = 0; i < keep_lines; ++i) {
    truncated += lines[i] + "\n";
  }
  int omitted = static_cast<int>(lines.size()) - keep_lines * 2;
  truncated += "\n... [" + std::to_string(omitted) + " lines omitted] ...\n\n";
  for (int i = static_cast<int>(lines.size()) - keep_lines;
       i < static_cast<int>(lines.size()); ++i) {
    truncated += lines[i] + "\n";
  }
  return truncated;
}

// Get context window size for a model name
static int get_context_window(const std::string& model) {
  // Anthropic models
  if (model.find("claude") != std::string::npos)
    return kContextWindow200K;
  // OpenAI models
  if (model.find("gpt-4o") != std::string::npos)
    return kContextWindow128K;
  if (model.find("gpt-4-turbo") != std::string::npos)
    return kContextWindow128K;
  if (model.find("gpt-4") != std::string::npos)
    return kContextWindow8K;
  if (model.find("gpt-3.5") != std::string::npos)
    return kContextWindow16K;
  // Qwen
  if (model.find("qwen") != std::string::npos)
    return kContextWindow128K;
  // DeepSeek
  if (model.find("deepseek") != std::string::npos)
    return kContextWindow128K;
  return kDefaultContextWindow;
}

AgentLoop::AgentLoop(std::shared_ptr<MemoryManager> memory_manager,
                     std::shared_ptr<SkillLoader> skill_loader,
                     std::shared_ptr<ToolRegistry> tool_registry,
                     std::shared_ptr<LLMProvider> llm_provider,
                     const AgentConfig& agent_config,
                     std::shared_ptr<spdlog::logger> logger)
    : memory_manager_(memory_manager),
      skill_loader_(skill_loader),
      tool_registry_(tool_registry),
      llm_provider_(llm_provider),
      turn_validator_(std::make_shared<TurnValidator>(logger)),
      context_pruner_(std::make_shared<ContextPruner>()),
      logger_(logger),
      agent_config_(agent_config),
      resolved_model_name_(agent_config.model) {
  // Use dynamic max iterations based on context window
  max_iterations_ = agent_config_.DynamicMaxIterations();
  logger_->info("AgentLoop initialized with model: {}, max_iterations: {}",
                agent_config_.model, max_iterations_);
}

std::shared_ptr<LLMProvider> AgentLoop::resolve_provider() {
  resolved_model_name_ = agent_config_.model;

  // If failover resolver is available, use it for profile rotation + fallback
  if (failover_resolver_) {
    auto resolved =
        failover_resolver_->Resolve(agent_config_.model, session_key_);
    if (resolved) {
      last_provider_id_ = resolved->provider_id;
      last_profile_id_ = resolved->profile_id;
      resolved_model_name_ = resolved->model;
      if (resolved->is_fallback) {
        logger_->info("Using fallback model: {}/{}", resolved->provider_id,
                      resolved->model);
      }
      return resolved->provider;
    }
    logger_->error("FailoverResolver exhausted all models/profiles for '{}'",
                   agent_config_.model);
    // Fall through to registry / injected provider
  }

  if (!provider_registry_) {
    last_provider_id_.clear();
    last_profile_id_.clear();
    return llm_provider_;
  }

  auto ref = provider_registry_->ResolveModel(agent_config_.model);
  auto provider = provider_registry_->GetProviderForModel(ref);
  if (provider) {
    last_provider_id_ = ref.provider;
    last_profile_id_.clear();
    resolved_model_name_ = ref.model;
    return provider;
  }

  logger_->warn(
      "Failed to resolve provider for model '{}', falling back to injected "
      "provider",
      agent_config_.model);
  last_provider_id_.clear();
  last_profile_id_.clear();
  return llm_provider_;
}

std::string AgentLoop::resolved_request_model() const {
  return resolved_model_name_.empty() ? agent_config_.model
                                      : resolved_model_name_;
}

void AgentLoop::SetModel(const std::string& model_ref) {
  agent_config_.model = model_ref;
  resolved_model_name_ = model_ref;
  logger_->info("Model set to: {}", model_ref);
}

// ---------------------------------------------------------------------------
// execute_with_retry — retry with exponential backoff, then failover chain
//
// Algorithm (mirrors OpenClaw pi-embedded-runner/run.ts):
// 1. Attempt the API call on the current provider/profile.
// 2. On retryable error, sleep with exponential backoff and retry up to
//    max_retries times on the SAME provider/profile.
// 3. After all retries exhausted, record failure with FailoverResolver
//    and re-resolve to the next available provider/profile.
// 4. Reset retry counter and repeat from step 1 with the new provider.
// 5. If FailoverResolver returns nullopt (all options exhausted), throw
//    a clear error describing the situation.
// 6. Non-retryable errors (auth, billing, invalid request) skip retries
//    but still attempt failover to a different provider.
// ---------------------------------------------------------------------------
ChatCompletionResponse AgentLoop::execute_with_retry(
    ChatCompletionRequest& request, std::shared_ptr<LLMProvider>& provider,
    const std::string& original_model, int max_retries) {
  int failover_attempts = 0;
  std::optional<ProviderError> last_provider_error;
  // Limit failover attempts to prevent infinite loops; fallback_chain size + 1
  // for the primary model, times profiles per provider.
  constexpr int kMaxFailoverAttempts = 20;

  while (failover_attempts < kMaxFailoverAttempts) {
    // Retry loop for the current provider/profile
    for (int retry = 0; retry <= max_retries; ++retry) {
      if (stop_requested_) {
        throw std::runtime_error("Agent turn stopped by user");
      }

      try {
        sanitize_request_messages_for_provider(provider, request,
                                               turn_validator_, logger_);
        auto response = provider->ChatCompletion(request);

        // Success — record with failover resolver
        if (failover_resolver_ && !last_provider_id_.empty()) {
          failover_resolver_->RecordSuccess(last_provider_id_, last_profile_id_,
                                            session_key_);
        }

        if (failover_attempts > 0) {
          logger_->info("Failover succeeded: provider={}, profile={}, model={}",
                        last_provider_id_, last_profile_id_, request.model);
        }

        return response;

      } catch (const ProviderError& pe) {
        last_provider_error = pe;
        logger_->warn(
            "Provider error on {}:{} (retry {}/{}, failover #{}): [{}] {}",
            last_provider_id_, last_profile_id_, retry, max_retries,
            failover_attempts, ProviderErrorKindToString(pe.Kind()), pe.what());

        // Context overflow is handled by the caller, not here
        if (pe.Kind() == ProviderErrorKind::kContextOverflow) {
          throw;
        }

        bool retryable = should_retry_provider_error(pe);

        // If retryable and we have retries left, backoff and retry same
        // provider
        if (retryable && retry < max_retries) {
          int backoff_sec =
              static_cast<int>(kRetryInitialBackoffSec *
                               std::pow(kRetryBackoffMultiplier, retry));
          backoff_sec = std::min(backoff_sec, kRetryMaxBackoffSec);

          // Honor Retry-After header if provided and larger
          if (pe.RetryAfterSeconds() > 0 &&
              pe.RetryAfterSeconds() > backoff_sec) {
            backoff_sec = std::min(pe.RetryAfterSeconds(), kRetryMaxBackoffSec);
          }

          logger_->info("Retrying in {}s (attempt {}/{})", backoff_sec,
                        retry + 1, max_retries);
          if (!interruptible_sleep(std::chrono::seconds(backoff_sec))) {
            logger_->info("Abort detected during retry backoff");
            throw std::runtime_error("Agent turn stopped by user");
          }
          continue;
        }

        // Retries exhausted (or non-retryable) — attempt failover
        if (failover_resolver_ && !last_provider_id_.empty()) {
          failover_resolver_->RecordFailure(last_provider_id_, last_profile_id_,
                                            pe.Kind(), pe.RetryAfterSeconds());
        }

        // Non-retryable errors that should not failover at all
        if (pe.Kind() == ProviderErrorKind::kAuthError ||
            pe.Kind() == ProviderErrorKind::kBillingError) {
          // Still try failover — a different provider may work
          logger_->warn(
              "Non-retryable error ({}), attempting failover to different "
              "provider",
              ProviderErrorKindToString(pe.Kind()));
        }

        // Break out of retry loop to attempt failover
        break;
      }
    }

    // --- Failover: try next provider/profile ---

    // Check abort before attempting failover
    if (stop_requested_) {
      logger_->info("Abort detected before failover attempt");
      throw std::runtime_error("Agent turn stopped by user");
    }

    if (!failover_resolver_) {
      if (last_provider_error.has_value()) {
        throw *last_provider_error;
      }
      throw std::runtime_error("All retries exhausted for provider " +
                               last_provider_id_ +
                               " and no failover resolver configured");
    }

    // Restore original model so FailoverResolver walks the full chain
    agent_config_.model = original_model;
    auto resolved = resolve_provider();

    if (!resolved || resolved == provider) {
      // resolve_provider returns the same provider if no alternative found,
      // or llm_provider_ as final fallback. Check if it's truly different.
      if (!resolved) {
        throw std::runtime_error(
            "All failover options exhausted. Tried " +
            std::to_string(failover_attempts + 1) + " provider(s) for model '" +
            original_model +
            "'. All providers/profiles are in cooldown or unavailable.");
      }
      // If we got back the same provider, all alternatives are exhausted
      if (failover_attempts > 0) {
        throw std::runtime_error(
            "Failover chain exhausted after " +
            std::to_string(failover_attempts + 1) +
            " attempt(s). No healthy provider available for model '" +
            original_model + "'.");
      }
    }

    provider = resolved;
    request.model = resolved_request_model();
    failover_attempts++;

    logger_->info(
        "Failover attempt #{}: switching to provider={}, profile={}, "
        "model={}",
        failover_attempts, last_provider_id_, last_profile_id_, request.model);
  }

  throw std::runtime_error("Maximum failover attempts (" +
                           std::to_string(kMaxFailoverAttempts) +
                           ") reached for model '" + original_model + "'");
}

bool AgentLoop::execute_stream_with_retry(
    ChatCompletionRequest& request, std::shared_ptr<LLMProvider>& provider,
    const std::string& original_model,
    const std::function<void(const ChatCompletionResponse&)>& stream_cb,
    int max_retries) {
  int failover_attempts = 0;
  constexpr int kMaxFailoverAttempts = 20;

  while (failover_attempts < kMaxFailoverAttempts) {
    for (int retry = 0; retry <= max_retries; ++retry) {
      if (stop_requested_) {
        return false;
      }

      try {
        sanitize_request_messages_for_provider(provider, request,
                                               turn_validator_, logger_);
        provider->ChatCompletionStream(request, stream_cb);

        // Success
        if (failover_resolver_ && !last_provider_id_.empty()) {
          failover_resolver_->RecordSuccess(last_provider_id_, last_profile_id_,
                                            session_key_);
        }

        if (failover_attempts > 0) {
          logger_->info(
              "Streaming failover succeeded: provider={}, profile={}, "
              "model={}",
              last_provider_id_, last_profile_id_, request.model);
        }

        return true;

      } catch (const ProviderError& pe) {
        logger_->warn(
            "Streaming provider error on {}:{} (retry {}/{}, failover "
            "#{}): [{}] {}",
            last_provider_id_, last_profile_id_, retry, max_retries,
            failover_attempts, ProviderErrorKindToString(pe.Kind()), pe.what());

        if (pe.Kind() == ProviderErrorKind::kContextOverflow) {
          throw;
        }

        bool retryable = should_retry_provider_error(pe);

        if (retryable && retry < max_retries) {
          int backoff_sec =
              static_cast<int>(kRetryInitialBackoffSec *
                               std::pow(kRetryBackoffMultiplier, retry));
          backoff_sec = std::min(backoff_sec, kRetryMaxBackoffSec);

          if (pe.RetryAfterSeconds() > 0 &&
              pe.RetryAfterSeconds() > backoff_sec) {
            backoff_sec = std::min(pe.RetryAfterSeconds(), kRetryMaxBackoffSec);
          }

          logger_->info("Streaming retry in {}s (attempt {}/{})", backoff_sec,
                        retry + 1, max_retries);
          if (!interruptible_sleep(std::chrono::seconds(backoff_sec))) {
            logger_->info("Abort detected during streaming retry backoff");
            return false;
          }
          continue;
        }

        if (failover_resolver_ && !last_provider_id_.empty()) {
          failover_resolver_->RecordFailure(last_provider_id_, last_profile_id_,
                                            pe.Kind(), pe.RetryAfterSeconds());
        }

        break;  // Exit retry loop, attempt failover
      }
    }

    // --- Failover ---

    // Check abort before attempting streaming failover
    if (stop_requested_) {
      logger_->info("Abort detected before streaming failover attempt");
      return false;
    }

    if (!failover_resolver_) {
      logger_->error("All streaming retries exhausted, no failover resolver");
      return false;
    }

    agent_config_.model = original_model;
    auto resolved = resolve_provider();

    if (!resolved || (resolved == provider && failover_attempts > 0)) {
      logger_->error(
          "Streaming failover chain exhausted after {} attempt(s) for "
          "model '{}'",
          failover_attempts + 1, original_model);
      return false;
    }

    provider = resolved;
    request.model = resolved_request_model();
    failover_attempts++;

    logger_->info(
        "Streaming failover attempt #{}: provider={}, profile={}, "
        "model={}",
        failover_attempts, last_provider_id_, last_profile_id_, request.model);
  }

  logger_->error("Maximum streaming failover attempts reached for model '{}'",
                 original_model);
  return false;
}

std::vector<Message> AgentLoop::ProcessMessage(
    const std::string& message, const std::vector<Message>& history,
    const std::string& system_prompt, const std::string& usage_session_key) {
  // Session write lock: serialize concurrent calls on the same AgentLoop
  // instance to prevent race conditions on session state.
  std::lock_guard<std::mutex> session_lock(session_write_mutex_);

  const std::string& effective_session_key =
      usage_session_key.empty() ? session_key_ : usage_session_key;
  logger_->info("Processing message (non-streaming)");
  stop_requested_ = false;

  // Check for duplicate messages
  std::string duplicate_notice;
  const bool suppress_duplicate_notice = is_brief_continuation_prompt(message);
  if (embedding_manager_ && !effective_session_key.empty()) {
    try {
      auto similar_results = embedding_manager_->SearchText(message, 10, 0.80f);
      int total_occurrences = 0;

      // Count how many times similar messages appeared
      for (const auto& result : similar_results) {
        try {
          auto metadata = result.metadata;
          if (metadata.contains("role") && metadata["role"] == "user" &&
              metadata.contains("session") && metadata["session"] == effective_session_key) {
            // Found similar user message in same session
            total_occurrences++;
            logger_->info("Found similar message: id={}, similarity={:.3f}, hit_count={}",
                         result.id, result.distance, metadata.value("hit_count", 1));
          }
        } catch (const std::exception& e) {
          logger_->warn("Failed to parse search result metadata: {}", e.what());
        }
      }

      // If found similar messages, this is a repeat (total_occurrences + 1 for current)
      if (total_occurrences > 0) {
        int repeat_count = total_occurrences + 1;
        if (suppress_duplicate_notice) {
          logger_->info(
              "Duplicate notice suppressed for brief continuation prompt "
              "(occurrences={})",
              repeat_count);
        } else {
          duplicate_notice =
              "[SYSTEM NOTICE: The user has sent a very similar message " +
              std::to_string(repeat_count) + " times. " +
              "Please acknowledge this repetition in your response and ask if "
              "there's something unclear or if they need different information.]";
          logger_->info("Duplicate message detected: total occurrences={}",
                        repeat_count);
        }
      }
    } catch (const std::exception& e) {
      logger_->warn("Failed to search for duplicate messages: {}", e.what());
    }
  }

  auto provider = resolve_provider();
  const int ctx_window = agent_config_.context_window > 0
                             ? agent_config_.context_window
                             : get_context_window(resolved_request_model());
  const auto request_tools = build_request_tools(tool_registry_);

  std::vector<Message> new_messages;

  // --- Auto-compaction: truncate history if too large ---
  std::vector<Message> effective_history = history;
  if (agent_config_.auto_compact && static_cast<int>(effective_history.size()) >
                                        agent_config_.compact_max_messages) {
    int keep = agent_config_.compact_keep_recent;
    int total = static_cast<int>(effective_history.size());
    if (total > keep) {
      int removed = total - keep;
      effective_history.assign(history.end() - keep, history.end());
      // Prepend truncation notice with context refresh instruction
      effective_history.insert(
          effective_history.begin(),
          Message{"system", "[Context compaction: " + std::to_string(removed) +
                                " earlier messages were removed. "
                                "Your system instructions remain active. "
                                "Refer to the system prompt for your identity "
                                "and capabilities.]"});
      logger_->info("Auto-compacted history: {} -> {} messages", total,
                    static_cast<int>(effective_history.size()));
    }
  }

  // --- Tool result pruning ---
  ContextPruner::Options prune_opts;
  effective_history = ContextPruner::Prune(effective_history, prune_opts);
  effective_history =
      focus_history_on_latest_turn(effective_history, message, logger_);
  effective_history = compress_history_for_context_budget(
      effective_history, system_prompt, message, request_tools, ctx_window,
      context_pruner_, logger_);

  // Build context: system + history + new user message
  std::vector<Message> context;

  // System message (always first, re-injected after compaction)
  if (!system_prompt.empty()) {
    context.push_back(Message{"system", system_prompt});
  }

  // Add duplicate notice if detected
  if (!duplicate_notice.empty()) {
    context.push_back(Message{"system", duplicate_notice});
  }

  // History
  for (const auto& msg : effective_history) {
    context.push_back(msg);
  }

  // New user message
  Message new_user_msg{"user", message};
  logger_->info("DEBUG: Creating new user message with {} content blocks",
                new_user_msg.content.size());
  for (size_t i = 0; i < new_user_msg.content.size(); ++i) {
    logger_->info("DEBUG: Content block {}: type={}, text_len={}",
                  i, new_user_msg.content[i].type, new_user_msg.content[i].text.size());
  }
  context.push_back(std::move(new_user_msg));

  // --- Context window guard: check estimated tokens vs limit ---
  int estimated = estimate_tokens(context);
  if (estimated + agent_config_.max_tokens >
      ctx_window - kContextWindowMinTokens) {
    logger_->warn(
        "Context window guard: estimated {} tokens + {} max_tokens exceeds "
        "window {} (min reserve {}). Forcing compaction.",
        estimated, agent_config_.max_tokens, ctx_window,
        kContextWindowMinTokens);
    // Force aggressive compaction: keep only last few messages
    int keep = std::min(agent_config_.compact_keep_recent,
                        static_cast<int>(context.size()));
    if (keep < static_cast<int>(context.size())) {
      std::vector<Message> compacted;
      if (!system_prompt.empty()) {
        compacted.push_back(Message{"system", system_prompt});
      }
      compacted.push_back(
          Message{"system",
                  "[Context compaction forced: context window nearly full. "
                  "Earlier messages removed.]"});
      for (auto it = context.end() - keep; it != context.end(); ++it) {
        compacted.push_back(*it);
      }
      context = std::move(compacted);
    }
  }

  // Create LLM request
  ChatCompletionRequest request;
  request.messages = context;
  request.model = resolved_request_model();
  request.temperature = agent_config_.temperature;
  request.max_tokens = agent_config_.max_tokens;
  request.thinking = agent_config_.thinking;

  // Add tool schemas
  request.tools = request_tools;
  request.tool_choice_auto = true;

  logger_->info("Request has {} tools", request.tools.size());
  if (request.tools.empty()) {
    logger_->error("CRITICAL: request_tools is empty! Tool registry may have failed.");
  }

  // CRITICAL: Force tool usage for search/lookup queries (ONLY for first request)
  bool force_first_tool_call = has_lookup_intent(message);
  if (force_first_tool_call) {
    request.tool_choice_type = "any";  // Force LLM to call a tool
    logger_->info("Detected lookup intent, forcing tool_choice=any for first request");
  } else {
    request.tool_choice_type = "auto";
  }

  // Save original model for failover re-resolution
  std::string original_model = agent_config_.model;
  int iterations = 0;
  int overflow_retries = 0;
  bool forced_lookup_retry = false;
  bool forced_web_search_synthesis_retry = false;

  while (iterations < max_iterations_ && !stop_requested_) {
    try {
      // Reset tool_choice to auto after first request (unless in Lookup Guard retry)
      if (iterations > 0 && !forced_lookup_retry) {
        request.tool_choice_type = "auto";
      }

      // Use execute_with_retry for retry + failover chain traversal.
      // Context overflow is re-thrown and handled below.
      auto response = execute_with_retry(request, provider, original_model);

      // --- Usage tracking ---
      if (usage_accumulator_ && !effective_session_key.empty()) {
        usage_accumulator_->Record(effective_session_key, response.usage);
      }
      logger_->debug("Token usage: prompt={} completion={}",
                     response.usage.prompt_tokens,
                     response.usage.completion_tokens);

      if (!response.tool_calls.empty()) {
        logger_->info("LLM requested {} tool calls",
                      response.tool_calls.size());

        std::vector<nlohmann::json> tool_calls_json;
        for (const auto& tc : response.tool_calls) {
          nlohmann::json tc_json;
          tc_json["id"] = tc.id;
          tc_json["function"]["name"] = tc.name;
          tc_json["function"]["arguments"] = tc.arguments.dump();
          tool_calls_json.push_back(tc_json);
        }
        auto tool_results = handle_tool_calls(tool_calls_json);

        // --- Tool result truncation fallback ---
        for (auto& result : tool_results) {
          result = truncate_tool_result(result, kToolResultMaxChars,
                                        kToolResultKeepLines);
        }

        // Assistant message: text + tool_use blocks
        Message assistant_msg;
        assistant_msg.role = "assistant";
        if (!response.content.empty())
          assistant_msg.content.push_back(
              ContentBlock::MakeText(response.content));
        for (const auto& tc : response.tool_calls)
          assistant_msg.content.push_back(
              ContentBlock::MakeToolUse(tc.id, tc.name, tc.arguments));
        request.messages.push_back(assistant_msg);
        new_messages.push_back(assistant_msg);

        // Tool results: single user message with tool_result blocks
        Message results_msg;
        results_msg.role = "user";
        for (size_t i = 0; i < response.tool_calls.size(); i++)
          results_msg.content.push_back(ContentBlock::MakeToolResult(
              response.tool_calls[i].id, tool_results[i]));
        request.messages.push_back(results_msg);
        new_messages.push_back(results_msg);

        // Check abort after tool execution — stop iterating immediately
        // rather than sending another LLM request with partial tool results.
        if (stop_requested_) {
          logger_->info(
              "Abort detected after tool execution, stopping iteration");
          break;
        }

        iterations++;
        continue;
      }

      if (!response.content.empty()) {
        const auto replay_tool_names =
            collect_trailing_replay_tool_names(request.messages);

        if (!forced_lookup_retry && has_lookup_intent(message) &&
            (response_looks_like_unfulfilled_lookup_preamble(response.content) ||
             response_contains_negative_conclusion(response.content))) {
          logger_->warn(
              "Lookup guard: provider returned a deferred search preamble "
              "or negative conclusion without tool calls; forcing one retry "
              "with explicit tool-use instruction");

          Message assistant_msg;
          assistant_msg.role = "assistant";
          assistant_msg.content.push_back(
              ContentBlock::MakeText(response.content));
          request.messages.push_back(std::move(assistant_msg));

          Message correction_msg;
          correction_msg.role = "user";
          correction_msg.content.push_back(ContentBlock::MakeText(
              "STOP. You MUST actually call the search tool. Here's an example:\n\n"
              "User: \"Search GitHub for Python projects\"\n"
              "WRONG: \"Let me search... I didn't find any results.\"\n"
              "CORRECT: [calls github_search_repos tool with query=\"python\"] → returns actual results\n\n"
              "Now YOU must call github_search_repos or web_search tool RIGHT NOW. "
              "DO NOT respond with text. ONLY call the tool."));
          request.messages.push_back(std::move(correction_msg));

          forced_lookup_retry = true;
          iterations++;
          continue;
        }

        if (!forced_web_search_synthesis_retry &&
            replay_tool_names.count("web_search") > 0 &&
            response_looks_like_raw_search_results_dump(response.content)) {
          logger_->warn(
              "Web search replay guard: provider echoed raw search results "
              "after tool replay; forcing one synthesis retry");

          Message assistant_msg;
          assistant_msg.role = "assistant";
          assistant_msg.content.push_back(
              ContentBlock::MakeText(response.content));
          request.messages.push_back(std::move(assistant_msg));

          Message correction_msg;
          correction_msg.role = "user";
          correction_msg.content.push_back(ContentBlock::MakeText(
              "Do not perform another web search and do not return a raw "
              "\"search results\" list. Use only the existing web_search "
              "tool results already in this conversation. Synthesize the "
              "most relevant matches, explain why they fit the user's "
              "request, and keep the answer concise."));
          request.messages.push_back(std::move(correction_msg));

          forced_web_search_synthesis_retry = true;
          iterations++;
          continue;
        }

        logger_->info("LLM provided final response");
        Message final_msg;
        final_msg.role = "assistant";
        final_msg.content.push_back(ContentBlock::MakeText(response.content));
        new_messages.push_back(final_msg);

        // Index messages to vector database if embedding manager is available
        IndexConversationMessages(message, new_messages, effective_session_key);

        return new_messages;
      }

      logger_->error("Unexpected LLM response format");
      break;

    } catch (const ProviderError& pe) {
      // --- Overflow compaction retry ---
      if (pe.Kind() == ProviderErrorKind::kContextOverflow &&
          overflow_retries < kOverflowCompactionMaxRetries) {
        overflow_retries++;
        logger_->warn(
            "Context overflow (attempt {}/{}), compacting and retrying",
            overflow_retries, kOverflowCompactionMaxRetries);
        int keep = std::max(2, static_cast<int>(request.messages.size()) / 2);
        std::vector<Message> compacted;
        if (!system_prompt.empty()) {
          compacted.push_back(Message{"system", system_prompt});
        }
        compacted.push_back(Message{
            "system", "[Context overflow recovery: older messages removed.]"});
        for (auto it = request.messages.end() - keep;
             it != request.messages.end(); ++it) {
          compacted.push_back(*it);
        }
        request.messages = std::move(compacted);
        continue;
      }

      // All retries, failover, and compaction exhausted
      logger_->error("Provider error after retry+failover exhausted: {}",
                     pe.what());
      throw;

    } catch (const std::exception& e) {
      logger_->error("Error in LLM processing: {}", e.what());
      throw;
    }
  }

  if (stop_requested_) {
    logger_->info("Agent turn aborted: returning {} partial message(s)",
                  new_messages.size());
    Message stop_msg;
    stop_msg.role = "assistant";
    stop_msg.content.push_back(
        ContentBlock::MakeText("[Agent turn stopped by user]"));
    new_messages.push_back(stop_msg);

    // Index messages even if stopped
    IndexConversationMessages(message, new_messages, effective_session_key);

    return new_messages;
  }

  throw std::runtime_error("Failed to get valid response after " +
                           std::to_string(max_iterations_) + " iterations");
}

std::vector<Message> AgentLoop::ProcessMessageStream(
    const std::string& message, const std::vector<Message>& history,
    const std::string& system_prompt, AgentEventCallback callback,
    const std::string& usage_session_key) {
  // Session write lock: serialize concurrent streaming calls on the same
  // AgentLoop instance.
  std::lock_guard<std::mutex> session_lock(session_write_mutex_);

  const std::string& effective_session_key =
      usage_session_key.empty() ? session_key_ : usage_session_key;
  logger_->info("Processing message (streaming)");
  stop_requested_ = false;

  auto provider = resolve_provider();
  const int ctx_window = agent_config_.context_window > 0
                             ? agent_config_.context_window
                             : get_context_window(resolved_request_model());
  const auto request_tools = build_request_tools(tool_registry_);

  std::vector<Message> new_messages;

  // --- Auto-compaction: truncate history if too large ---
  std::vector<Message> effective_history = history;
  if (agent_config_.auto_compact && static_cast<int>(effective_history.size()) >
                                        agent_config_.compact_max_messages) {
    int keep = agent_config_.compact_keep_recent;
    int total = static_cast<int>(effective_history.size());
    if (total > keep) {
      int removed = total - keep;
      effective_history.assign(history.end() - keep, history.end());
      effective_history.insert(
          effective_history.begin(),
          Message{"system", "[Context compaction: " + std::to_string(removed) +
                                " earlier messages were removed. "
                                "Your system instructions remain active. "
                                "Refer to the system prompt for your identity "
                                "and capabilities.]"});
      logger_->info("Auto-compacted streaming history: {} -> {} messages",
                    total, static_cast<int>(effective_history.size()));
    }
  }

  // --- Tool result pruning ---
  ContextPruner::Options prune_opts;
  effective_history = ContextPruner::Prune(effective_history, prune_opts);
  effective_history =
      focus_history_on_latest_turn(effective_history, message, logger_);
  effective_history = compress_history_for_context_budget(
      effective_history, system_prompt, message, request_tools, ctx_window,
      context_pruner_, logger_);

  // Build context (system prompt always re-injected first)
  std::vector<Message> context;
  if (!system_prompt.empty()) {
    context.push_back(Message{"system", system_prompt});
  }
  for (const auto& msg : effective_history) {
    context.push_back(msg);
  }
  Message new_user_msg{"user", message};
  logger_->info("DEBUG: Creating new user message (stream) with {} content blocks",
                new_user_msg.content.size());
  for (size_t i = 0; i < new_user_msg.content.size(); ++i) {
    logger_->info("DEBUG: Content block {}: type={}, text_len={}",
                  i, new_user_msg.content[i].type, new_user_msg.content[i].text.size());
  }
  context.push_back(std::move(new_user_msg));

  // --- Context window guard ---
  int estimated = estimate_tokens(context);
  if (estimated + agent_config_.max_tokens >
      ctx_window - kContextWindowMinTokens) {
    logger_->warn("Streaming context window guard triggered: {} + {} > {} - {}",
                  estimated, agent_config_.max_tokens, ctx_window,
                  kContextWindowMinTokens);
    int keep = std::min(agent_config_.compact_keep_recent,
                        static_cast<int>(context.size()));
    if (keep < static_cast<int>(context.size())) {
      std::vector<Message> compacted;
      if (!system_prompt.empty()) {
        compacted.push_back(Message{"system", system_prompt});
      }
      compacted.push_back(
          Message{"system",
                  "[Context compaction forced: context window nearly full.]"});
      for (auto it = context.end() - keep; it != context.end(); ++it) {
        compacted.push_back(*it);
      }
      context = std::move(compacted);
    }
  }

  ChatCompletionRequest request;
  request.messages = context;
  request.model = resolved_request_model();
  request.temperature = agent_config_.temperature;
  request.max_tokens = agent_config_.max_tokens;
  request.stream = true;
  request.thinking = agent_config_.thinking;

  request.tools = request_tools;
  request.tool_choice_auto = true;

  std::string original_model_stream = agent_config_.model;
  int iterations = 0;
  int overflow_retries_stream = 0;

  while (iterations < max_iterations_ && !stop_requested_) {
    try {
      sanitize_request_messages_for_provider(provider, request, turn_validator_,
                                             logger_);
      std::string full_response;
      TokenUsage stream_usage;

      provider->ChatCompletionStream(
          request, [&](const ChatCompletionResponse& chunk) {
            if (!chunk.content.empty()) {
              full_response += chunk.content;
              if (callback) {
                callback({events::kTextDelta, {{"text", chunk.content}}});
              }
            }

            // Accumulate usage from stream chunks
            stream_usage.prompt_tokens += chunk.usage.prompt_tokens;
            stream_usage.completion_tokens += chunk.usage.completion_tokens;

            if (!chunk.tool_calls.empty()) {
              for (const auto& tc : chunk.tool_calls) {
                if (callback) {
                  callback({events::kToolUse,
                            {{"id", tc.id},
                             {"name", tc.name},
                             {"input", tc.arguments}}});
                }

                // Construct assistant message with text + tool_use blocks
                Message assistant_msg;
                assistant_msg.role = "assistant";
                if (!full_response.empty())
                  assistant_msg.content.push_back(
                      ContentBlock::MakeText(full_response));
                assistant_msg.content.push_back(
                    ContentBlock::MakeToolUse(tc.id, tc.name, tc.arguments));
                request.messages.push_back(assistant_msg);
                new_messages.push_back(assistant_msg);
                full_response.clear();

                // Execute tool
                if (stop_requested_) {
                  logger_->info(
                      "Abort detected: skipping tool execution for {}",
                      tc.name);
                  std::string abort_content =
                      "[Tool execution aborted by user]";
                  if (callback) {
                    callback({events::kToolResult,
                              {{"tool_use_id", tc.id},
                               {"content", abort_content},
                               {"is_error", true}}});
                  }

                  Message results_msg;
                  results_msg.role = "user";
                  results_msg.content.push_back(
                      ContentBlock::MakeToolResult(tc.id, abort_content));
                  request.messages.push_back(results_msg);
                  new_messages.push_back(results_msg);
                } else {
                  try {
                    auto result =
                        tool_registry_->ExecuteTool(tc.name, tc.arguments);
                    // --- Tool result truncation ---
                    result = truncate_tool_result(result, kToolResultMaxChars,
                                                  kToolResultKeepLines);
                    if (callback) {
                      callback({events::kToolResult,
                                {{"tool_use_id", tc.id}, {"content", result}}});
                    }

                    Message results_msg;
                    results_msg.role = "user";
                    results_msg.content.push_back(
                        ContentBlock::MakeToolResult(tc.id, result));
                    request.messages.push_back(results_msg);
                    new_messages.push_back(results_msg);
                  } catch (const std::exception& e) {
                    std::string error_content =
                        "Error: " + std::string(e.what());
                    if (callback) {
                      callback({events::kToolResult,
                                {{"tool_use_id", tc.id},
                                 {"content", error_content},
                                 {"is_error", true}}});
                    }

                    Message results_msg;
                    results_msg.role = "user";
                    results_msg.content.push_back(
                        ContentBlock::MakeToolResult(tc.id, error_content));
                    request.messages.push_back(results_msg);
                    new_messages.push_back(results_msg);
                  }
                }  // end abort check else
              }
              iterations++;
              return;  // Continue loop for tool results
            }

            if (chunk.is_stream_end) {
              if (callback) {
                callback({events::kMessageEnd, {{"content", full_response}}});
              }
            }
          });

      // --- Usage tracking ---
      if (usage_accumulator_ && !effective_session_key.empty()) {
        usage_accumulator_->Record(effective_session_key, stream_usage);
      }
      logger_->debug("Token usage (stream): prompt={} completion={}",
                     stream_usage.prompt_tokens,
                     stream_usage.completion_tokens);

      // Record success for failover tracking
      if (failover_resolver_ && !last_provider_id_.empty()) {
        failover_resolver_->RecordSuccess(last_provider_id_, last_profile_id_,
                                          session_key_);
      }

      // If we got a final response without tool calls, we're done
      if (!full_response.empty()) {
        Message final_msg;
        final_msg.role = "assistant";
        final_msg.content.push_back(ContentBlock::MakeText(full_response));
        new_messages.push_back(final_msg);

        // Index messages to vector database if embedding manager is available
        if (embedding_manager_ && !effective_session_key.empty()) {
          try {
            // Index user message
            if (!message.empty()) {
              std::string user_msg_id = effective_session_key + ":user:" +
                  std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
              nlohmann::json user_metadata = {
                {"role", "user"},
                {"session", effective_session_key},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
              };
              embedding_manager_->IndexText(user_msg_id, message, user_metadata);
              logger_->debug("Indexed user message (stream): {}", user_msg_id);
            }

            // Index assistant response
            if (!full_response.empty()) {
              std::string assistant_msg_id = effective_session_key + ":assistant:" +
                  std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
              nlohmann::json assistant_metadata = {
                {"role", "assistant"},
                {"session", effective_session_key},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
              };
              embedding_manager_->IndexText(assistant_msg_id, full_response, assistant_metadata);
              logger_->debug("Indexed assistant message (stream): {}", assistant_msg_id);
            }
          } catch (const std::exception& e) {
            logger_->warn("Failed to index messages to vector database: {}", e.what());
          }
        }

        return new_messages;
      }

      iterations++;

    } catch (const ProviderError& pe) {
      // --- Overflow compaction retry ---
      if (pe.Kind() == ProviderErrorKind::kContextOverflow &&
          overflow_retries_stream < kOverflowCompactionMaxRetries) {
        overflow_retries_stream++;
        logger_->warn("Streaming context overflow (attempt {}/{}), compacting",
                      overflow_retries_stream, kOverflowCompactionMaxRetries);
        int keep = std::max(2, static_cast<int>(request.messages.size()) / 2);
        std::vector<Message> compacted;
        if (!system_prompt.empty()) {
          compacted.push_back(Message{"system", system_prompt});
        }
        compacted.push_back(Message{
            "system", "[Context overflow recovery: older messages removed.]"});
        for (auto it = request.messages.end() - keep;
             it != request.messages.end(); ++it) {
          compacted.push_back(*it);
        }
        request.messages = std::move(compacted);
        continue;
      }

      // Context overflow with retries exhausted — throw immediately
      if (pe.Kind() == ProviderErrorKind::kContextOverflow) {
        logger_->error("Streaming context overflow: retries exhausted");
        if (callback) {
          callback({events::kMessageEnd, {{"error", pe.what()}}});
        }
        return new_messages;
      }

      // Record failure and attempt failover (with Retry-After if provided)
      if (failover_resolver_ && !last_provider_id_.empty()) {
        failover_resolver_->RecordFailure(last_provider_id_, last_profile_id_,
                                          pe.Kind(), pe.RetryAfterSeconds());

        // Check abort before attempting failover
        if (stop_requested_) {
          logger_->info(
              "Abort detected before streaming ProcessMessage failover");
          break;
        }

        logger_->warn("Streaming provider error ({}), attempting failover: {}",
                      ProviderErrorKindToString(pe.Kind()), pe.what());

        agent_config_.model = original_model_stream;
        auto new_provider = resolve_provider();
        if (new_provider && new_provider != provider) {
          provider = new_provider;
          request.model = resolved_request_model();
          iterations++;
          continue;
        }
      }

      logger_->error("Error in streaming: {}", pe.what());
      if (callback) {
        callback({events::kMessageEnd, {{"error", pe.what()}}});
      }
      return new_messages;

    } catch (const std::exception& e) {
      logger_->error("Error in streaming: {}", e.what());
      if (callback) {
        callback({events::kMessageEnd, {{"error", e.what()}}});
      }
      return new_messages;
    }
  }

  std::string stop_text =
      stop_requested_ ? "[Stopped]" : "[Max iterations reached]";
  if (callback) {
    callback({events::kMessageEnd, {{"content", stop_text}}});
  }
  Message stop_msg;
  stop_msg.role = "assistant";
  stop_msg.content.push_back(ContentBlock::MakeText(stop_text));
  new_messages.push_back(stop_msg);
  return new_messages;
}

void AgentLoop::IndexConversationMessages(
    const std::string& user_message,
    const std::vector<Message>& new_messages,
    const std::string& session_key) {
  logger_->info("IndexConversationMessages called: embedding_manager_={}, session_key='{}'",
               (void*)embedding_manager_.get(), session_key);

  if (!embedding_manager_ || session_key.empty()) {
    logger_->warn("Skipping indexing: embedding_manager_={}, session_key='{}'",
                 (void*)embedding_manager_.get(), session_key);
    return;
  }

  try {
    logger_->info("Indexing conversation messages for session: {}", session_key);

    // Index user message with duplicate detection
    if (!user_message.empty()) {
      // Search for similar messages to track hit count
      auto similar_results = embedding_manager_->SearchText(user_message, 10, 0.80f);

      int hit_count = 1;
      std::string original_id;

      // Count similar messages in same session
      for (const auto& result : similar_results) {
        try {
          auto metadata = result.metadata;
          if (metadata.contains("role") && metadata["role"] == "user" &&
              metadata.contains("session") && metadata["session"] == session_key) {
            // Found similar user message in same session
            hit_count++;
            if (original_id.empty()) {
              original_id = result.id;
            }
          }
        } catch (const std::exception& e) {
          logger_->warn("Failed to parse metadata: {}", e.what());
        }
      }

      std::string user_msg_id = session_key + ":user:" +
          std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
      nlohmann::json user_metadata = {
        {"role", "user"},
        {"session", session_key},
        {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
        {"hit_count", hit_count}
      };

      if (!original_id.empty()) {
        user_metadata["original_id"] = original_id;
      }

      logger_->debug("Indexing user message: {} (hit_count: {})", user_msg_id, hit_count);
      logger_->debug("Calling embedding_manager_->IndexText...");
      bool result = embedding_manager_->IndexText(user_msg_id, user_message, user_metadata.dump());
      logger_->debug("IndexText returned: {}", result);
    }

    // Index all assistant messages
    for (const auto& msg : new_messages) {
      if (msg.role != "assistant") {
        continue;
      }

      // Extract text content from message
      std::string content_text;
      for (const auto& block : msg.content) {
        if (block.type == "text" && !block.text.empty()) {
          if (!content_text.empty()) {
            content_text += "\n";
          }
          content_text += block.text;
        }
      }

      if (!content_text.empty()) {
        std::string assistant_msg_id = session_key + ":assistant:" +
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        nlohmann::json assistant_metadata = {
          {"role", "assistant"},
          {"session", session_key},
          {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
        };
        logger_->debug("Indexing assistant message: {}", assistant_msg_id);
        embedding_manager_->IndexText(assistant_msg_id, content_text, assistant_metadata.dump());
      }
    }

    logger_->info("Conversation indexing completed");
  } catch (const std::exception& e) {
    logger_->warn("Failed to index conversation messages: {}", e.what());
  }
}

void AgentLoop::Stop() {
  bool was_running = !stop_requested_.exchange(true);
  if (was_running) {
    logger_->info("Abort requested: stopping agent loop and cleaning up");
  } else {
    logger_->debug("Abort requested but already stopping");
  }
}

bool AgentLoop::interruptible_sleep(std::chrono::seconds duration) {
  // Sleep in 100ms increments, checking abort flag each time.
  // Returns true if full duration elapsed, false if interrupted.
  auto end = std::chrono::steady_clock::now() + duration;
  while (std::chrono::steady_clock::now() < end) {
    if (stop_requested_) {
      return false;
    }
    auto remaining = end - std::chrono::steady_clock::now();
    auto sleep_chunk = std::min(
        std::chrono::duration_cast<std::chrono::milliseconds>(remaining),
        std::chrono::milliseconds(100));
    if (sleep_chunk.count() > 0) {
      std::this_thread::sleep_for(sleep_chunk);
    }
  }
  return !stop_requested_.load();
}

void AgentLoop::SetConfig(const AgentConfig& config) {
  agent_config_ = config;
  resolved_model_name_ = config.model;
  max_iterations_ = config.DynamicMaxIterations();
  logger_->info(
      "AgentLoop config updated: model={}, temp={}, max_tokens={}, "
      "max_iterations={}, thinking={}",
      config.model, config.temperature, config.max_tokens, max_iterations_,
      config.thinking);
}

std::vector<std::string>
AgentLoop::handle_tool_calls(const std::vector<nlohmann::json>& tool_calls) {
  std::vector<std::string> results;

  for (const auto& tool_call : tool_calls) {
    // Check abort flag before each tool execution
    if (stop_requested_) {
      logger_->info("Abort detected: skipping remaining {} tool call(s)",
                    tool_calls.size() - results.size());
      for (size_t i = results.size(); i < tool_calls.size(); ++i) {
        results.push_back("[Tool execution aborted by user]");
      }
      break;
    }

    try {
      std::string tool_name = tool_call["function"]["name"];
      nlohmann::json arguments;
      const auto& args_val = tool_call["function"]["arguments"];
      if (args_val.is_string()) {
        arguments = nlohmann::json::parse(args_val.get<std::string>());
      } else {
        arguments = args_val;
      }

      logger_->info("Executing tool: {} with arguments: {}", tool_name,
                    arguments.dump());
      std::string result = tool_registry_->ExecuteTool(tool_name, arguments);
      results.push_back(result);
      logger_->info("Tool execution successful");

    } catch (const std::exception& e) {
      logger_->error("Tool execution failed: {}", e.what());
      results.push_back("Error executing tool: " + std::string(e.what()));
    }
  }

  return results;
}

}  // namespace ravbot
