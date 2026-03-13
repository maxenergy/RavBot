// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "quantclaw/core/content_block.hpp"
#include "quantclaw/common/noncopyable.hpp"

namespace quantclaw {

// --- Session Key Utilities (OpenClaw compatible) ---

struct ParsedSessionKey {
    std::string agent_id;
    std::string rest;
};

// Parse a session key into agent_id and rest.
// Returns nullopt if the key doesn't match the "agent:<agentId>:<rest>" format.
std::optional<ParsedSessionKey> ParseAgentSessionKey(const std::string& key);

// Normalize a session key to OpenClaw format.
// - Plain keys like "my-session" → "agent:main:my-session"
// - Keys missing the "agent:" prefix get it added
// - Agent ID and rest are lowercased
std::string NormalizeSessionKey(const std::string& key,
                                 const std::string& default_agent_id = "main");

// Build the default main session key: "agent:<agentId>:main"
std::string BuildMainSessionKey(const std::string& agent_id = "main");

// --- Usage Info ---

struct UsageInfo {
    int input_tokens = 0;
    int output_tokens = 0;

    nlohmann::json ToJson() const {
        return {{"inputTokens", input_tokens}, {"outputTokens", output_tokens}};
    }
    static UsageInfo FromJson(const nlohmann::json& j) {
        UsageInfo u;
        u.input_tokens = j.value("inputTokens", 0);
        u.output_tokens = j.value("outputTokens", 0);
        return u;
    }
};

// --- Session Message (JSONL line format) ---

struct SessionMessage {
    std::string role;  // "user" | "assistant" | "system" | "tool"
    std::vector<ContentBlock> content;
    std::string timestamp;  // ISO 8601
    std::optional<UsageInfo> usage;

    nlohmann::json ToJsonl() const;
    static SessionMessage FromJsonl(const nlohmann::json& j);
};

// --- Session Info (sessions.json entry) ---

struct SessionInfo {
    std::string session_key;
    std::string session_id;
    std::string updated_at;
    std::string created_at;
    std::string display_name;
    std::string channel;
};

// --- Session Policy ---
// Requirements: 11.1, 11.2, 11.3

struct SessionPolicy {
    std::optional<std::string> model_override;
    std::vector<std::string> tool_whitelist;
    std::optional<std::string> output_format;
    std::optional<int> rate_limit;  // Messages per minute
    std::map<std::string, std::string> custom_settings;

    nlohmann::json ToJson() const;
    static SessionPolicy FromJson(const nlohmann::json& j);
};

// --- Transcript Event ---
// Requirements: 8.1, 8.2, 8.3

struct TranscriptEvent {
    std::string event_type;  // "message_added", "message_modified", "message_deleted"
    std::string session_key;
    std::string timestamp;
    nlohmann::json data;

    nlohmann::json ToJson() const;
};

using TranscriptEventCallback = std::function<void(const TranscriptEvent&)>;

struct TranscriptSubscription {
    std::string subscription_id;
    TranscriptEventCallback callback;
};

// --- Session Handle ---

struct SessionHandle {
    std::string session_key;
    std::string session_id;
    std::filesystem::path transcript_path;
};

// --- Session Manager ---

class SessionManager : public Noncopyable {
public:
    SessionManager(const std::filesystem::path& sessions_dir,
                   std::shared_ptr<spdlog::logger> logger);

    // Get or create a session by key
    SessionHandle GetOrCreate(const std::string& session_key,
                                const std::string& display_name = "",
                                const std::string& channel = "cli");

    // Append a message to the session transcript
    void AppendMessage(const std::string& session_key,
                        const std::string& role,
                        const std::string& text_content,
                        const std::optional<UsageInfo>& usage = std::nullopt);

    // Append a full SessionMessage
    void AppendMessage(const std::string& session_key, const SessionMessage& msg);

    // Append a thinking_level_change entry (OpenClaw JSONL compat)
    void AppendThinkingLevelChange(const std::string& session_key,
                                    const std::string& thinking_level);

    // Append a custom_message entry (OpenClaw JSONL compat)
    // custom_type: arbitrary string identifier; content/display/details are optional JSON
    void AppendCustomMessage(const std::string& session_key,
                              const std::string& custom_type,
                              const nlohmann::json& content = nlohmann::json::array(),
                              const nlohmann::json& display = nlohmann::json::object(),
                              const nlohmann::json& details = nlohmann::json::object());

    // Get session history
    std::vector<SessionMessage> GetHistory(const std::string& session_key,
                                            int max_messages = -1) const;

    // List all sessions
    std::vector<SessionInfo> ListSessions() const;

    // Delete a session entirely
    void DeleteSession(const std::string& session_key);

    // Reset a session (archive old, create new session ID)
    void ResetSession(const std::string& session_key);

    // Update display name
    void UpdateDisplayName(const std::string& session_key, const std::string& name);

    // Set session policy
    // Requirements: 11.1, 11.2
    void SetPolicy(const std::string& session_key, const SessionPolicy& policy);

    // Get session policy
    // Requirements: 11.3
    SessionPolicy GetPolicy(const std::string& session_key) const;

    // Subscribe to transcript events
    // Requirements: 8.1, 8.2, 8.3
    std::string Subscribe(const std::string& session_key,
                          TranscriptEventCallback callback);

    // Unsubscribe from events
    // Requirements: 8.7
    void Unsubscribe(const std::string& session_key,
                     const std::string& subscription_id);

    // Persistence
    void SaveStore();
    void LoadStore();

private:
    std::filesystem::path sessions_dir_;
    std::shared_ptr<spdlog::logger> logger_;
    mutable std::shared_mutex mutex_;  // shared for reads, exclusive for writes

    // session_key -> SessionInfo
    std::unordered_map<std::string, SessionInfo> store_;

    // Policy store
    // Requirements: 11.1, 11.2
    std::unordered_map<std::string, SessionPolicy> policies_;
    mutable std::shared_mutex policy_mutex_;

    // Event subscribers
    // Requirements: 8.1, 8.2
    std::unordered_map<std::string, std::vector<TranscriptSubscription>> subscribers_;
    mutable std::shared_mutex subscriber_mutex_;
    uint64_t subscription_counter_ = 0;

    std::string generate_session_id() const;
    std::string get_timestamp() const;
    std::filesystem::path transcript_path(const std::string& session_id) const;

    // Emit transcript event
    // Requirements: 8.2, 8.6
    void emit_event(const std::string& session_key, const TranscriptEvent& event);

    // Shared boilerplate: normalize key, look up session, open transcript,
    // write entry, update updated_at, and SaveStore. Returns true on success.
    // Caller must NOT hold mutex_ when calling this.
    bool AppendTranscriptEntry(const std::string& session_key,
                               const nlohmann::json& entry);
};

} // namespace quantclaw
