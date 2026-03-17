// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include "ravbot/security/trust_model.hpp"

namespace ravbot {

class MemoryManager;
class SkillLoader;
class ToolRegistry;

struct AgentConfig;
struct RavBotConfig;

// Prompt verbosity level (mirrors OpenClaw PromptMode).
enum class PromptMode {
  kFull,     // Full prompt with all sections including safety constitution
  kMinimal,  // Identity + tools only, no safety constitution
  kNone,     // Bare minimum — identity string only
};

// Context information for prompt building
struct PromptContext {
  std::string channel;              // Source channel (e.g., "telegram", "cli")
  std::string sender_id;            // Sender identifier
  std::string session_key;          // Session key
  int tool_count = 0;               // Number of available tools
  int context_window_size = 0;      // Model's context window size
  std::map<std::string, std::string> metadata;  // Additional metadata
};

// Component flags for selective prompt building
struct PromptComponents {
  bool include_skills_protocol = true;       // Include skill calling format
  bool include_memory_rules = true;          // Include memory recall rules
  bool include_channel_instructions = true;  // Include channel-specific behavior
  bool include_runtime_metadata = true;      // Include time, workspace, platform
  bool include_sender_trust = true;          // Include sender trust level info
  bool include_output_constraints = true;    // Include output format constraints
  bool include_operation_guards = true;      // Include dangerous operation guards
  bool use_tool_summary = false;  // Use summary for >100 tools (auto-detect)
  // New fields aligning with OpenClaw PromptComponents
  bool include_silent_reply = true;          // Include [SILENT] token usage rules
  bool include_safety_constitution = true;   // Include AI safety constitution
  PromptMode mode = PromptMode::kFull;       // Verbosity level control
};

class PromptBuilder {
public:
    PromptBuilder(std::shared_ptr<MemoryManager> memory_manager,
                  std::shared_ptr<SkillLoader> skill_loader,
                  std::shared_ptr<ToolRegistry> tool_registry,
                  const RavBotConfig* config = nullptr);

    // Full system prompt: SOUL + AGENTS + TOOLS + skills + memory + runtime info
    std::string BuildFull(const std::string& agent_id = "default") const;

    // Minimal system prompt: identity + tools only
    std::string BuildMinimal(const std::string& agent_id = "default") const;

    // Build with specific components and context
    std::string BuildWithComponents(const PromptComponents& components,
                                    const PromptContext& context = {}) const;

    // Set channel-specific instructions
    void SetChannelInstructions(const std::string& channel,
                                const std::string& instructions);

    // Set sender trust level
    void SetSenderTrust(const std::string& sender_id, TrustLevel level);

private:
    std::shared_ptr<MemoryManager> memory_manager_;
    std::shared_ptr<SkillLoader> skill_loader_;
    std::shared_ptr<ToolRegistry> tool_registry_;
    const RavBotConfig* config_ = nullptr;

    // Channel-specific instructions storage
    std::map<std::string, std::string> channel_instructions_;
    
    // Sender trust levels storage
    std::map<std::string, TrustLevel> sender_trust_;

    // Existing helper methods
    std::string get_section(const std::string& filename) const;
    std::string get_runtime_info() const;

    // New component building methods (Requirements 1.1-1.7)
    std::string build_skills_protocol() const;
    std::string build_memory_recall_rules() const;
    std::string build_channel_instructions(const std::string& channel) const;
    std::string build_runtime_metadata() const;
    std::string build_sender_trust_info(const std::string& sender_id) const;
    std::string build_output_constraints() const;
    std::string build_operation_guards() const;
    std::string build_tool_summary() const;  // For >100 tools (Requirement 1.8)
    // Safety and alignment sections (aligns with OpenClaw)
    std::string build_silent_reply_section() const;
    std::string build_safety_constitution() const;
};

} // namespace ravbot
