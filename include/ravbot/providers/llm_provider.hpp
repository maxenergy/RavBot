// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <memory>
#include "ravbot/core/content_block.hpp"

namespace ravbot {

struct Message {
    std::string role;
    std::vector<ContentBlock> content;
    // Optional timestamp (ISO-8601 or epoch ms string).
    // Preserved by TurnValidator::merge_consecutive_role() — the merged
    // message takes the timestamp of the most recent constituent message.
    std::string timestamp;

    Message() = default;
    Message(std::string role, std::string text)
        : role(std::move(role)) {
        if (!text.empty()) content.push_back(ContentBlock::MakeText(std::move(text)));
    }
    std::string text() const {
        std::string r;
        for (const auto& b : content)
            if (b.type == "text" || b.type == "thinking") r += b.text;
        return r;
    }
};

struct ToolCall {
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

struct ChatCompletionRequest {
    std::vector<Message> messages;
    std::string model;
    double temperature = 0.7;
    int max_tokens = 8192;
    std::vector<nlohmann::json> tools;
    bool tool_choice_auto = true;
    std::string tool_choice_type = "auto";  // "auto" | "any" | "tool"
    std::string tool_choice_name = "";      // tool name when type="tool"
    bool stream = false;
    std::string thinking = "off";  // "off" | "low" | "medium" | "high"
};

struct TokenUsage {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
    // Anthropic prompt-caching fields (aligns with OpenClaw UsageAccumulator)
    int cache_read_input_tokens = 0;   // cache_read_input_tokens in API response
    int cache_creation_input_tokens = 0;  // cache_creation_input_tokens
};

struct ChatCompletionResponse {
    std::string content;
    std::vector<ToolCall> tool_calls;
    std::string finish_reason;
    bool is_stream_end = false;
    TokenUsage usage;
};

class LLMProvider {
public:
    virtual ~LLMProvider() = default;
    
    virtual ChatCompletionResponse ChatCompletion(const ChatCompletionRequest& request) = 0;
    virtual void ChatCompletionStream(const ChatCompletionRequest& request,
                                     std::function<void(const ChatCompletionResponse&)> callback) = 0;
    virtual std::string GetProviderName() const = 0;
    virtual std::vector<std::string> GetSupportedModels() const = 0;
};

} // namespace ravbot