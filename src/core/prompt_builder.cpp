// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/prompt_builder.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "quantclaw/config.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/tools/tool_registry.hpp"

namespace quantclaw {

PromptBuilder::PromptBuilder(std::shared_ptr<MemoryManager> memory_manager,
                             std::shared_ptr<SkillLoader> skill_loader,
                             std::shared_ptr<ToolRegistry> tool_registry,
                             const QuantClawConfig* config)
    : memory_manager_(memory_manager),
      skill_loader_(skill_loader),
      tool_registry_(tool_registry),
      config_(config) {}

void PromptBuilder::SetChannelInstructions(const std::string& channel,
                                           const std::string& instructions) {
  channel_instructions_[channel] = instructions;
}

void PromptBuilder::SetSenderTrust(const std::string& sender_id,
                                   TrustLevel level) {
  sender_trust_[sender_id] = level;
}

std::string PromptBuilder::BuildFull(const std::string& /*agent_id*/) const {
  std::ostringstream prompt;

  // 1. SOUL.md - identity
  auto soul = get_section("SOUL.md");
  if (!soul.empty()) {
    prompt << "## Your Identity\n" << soul << "\n\n";
  }

  // 2. AGENTS.md - behavior instructions (OpenClaw)
  auto agents = get_section("AGENTS.md");
  if (!agents.empty()) {
    prompt << "## Agent Behavior\n" << agents << "\n\n";
  }

  // 3. TOOLS.md - tool usage guide (OpenClaw)
  auto tools = get_section("TOOLS.md");
  if (!tools.empty()) {
    prompt << "## Tool Usage Guide\n" << tools << "\n\n";
  }

  // 4. Loaded skills (multi-dir if config available, single-dir fallback)
  std::vector<SkillMetadata> skills;
  if (config_) {
    skills = skill_loader_->LoadSkills(config_->skills,
                                       memory_manager_->GetWorkspacePath());
  } else {
    skills = skill_loader_->LoadSkillsFromDirectory(
        memory_manager_->GetWorkspacePath() / "skills");
  }
  if (!skills.empty()) {
    prompt << "## Available Skills\n"
           << skill_loader_->GetSkillContext(skills) << "\n\n";
  }

  // 5. Memory context (recent daily memory)
  try {
    auto memory_content = get_section("MEMORY.md");
    if (!memory_content.empty()) {
      prompt << "## Memory\n" << memory_content << "\n\n";
    }
  } catch (const std::exception&) {}

  // 6. Runtime info
  prompt << "## Runtime Information\n" << get_runtime_info() << "\n\n";

  // 7. Conversation focus
  prompt << "## Conversation Focus\n"
         << "- Prioritize the user's latest message over older chat history.\n"
         << "- For actions like search, read, check, or research, ground the "
            "action in the latest user message unless the user explicitly "
            "refers back.\n"
         << "- If the target of a request is unclear, ask a short clarifying "
            "question instead of guessing from older context.\n\n";

  // 8. Available tools
  auto tool_schemas = tool_registry_->GetToolSchemas();
  if (!tool_schemas.empty()) {
    prompt << "## Available Tools\n";
    for (const auto& schema : tool_schemas) {
      prompt << "- **" << schema.name << "**: " << schema.description << "\n";
    }
    prompt << "\n";
  }

  // Default identity fallback
  prompt << "You are QuantClaw, a high-performance C++ personal AI assistant. "
         << "Use the available tools when needed to help the user. "
         << "Always be concise and helpful.";

  return prompt.str();
}

std::string PromptBuilder::BuildMinimal(const std::string& /*agent_id*/) const {
  std::ostringstream prompt;

  // Identity only
  auto soul = get_section("SOUL.md");
  if (!soul.empty()) {
    prompt << "## Your Identity\n" << soul << "\n\n";
  }

  // Tools
  auto tool_schemas = tool_registry_->GetToolSchemas();
  if (!tool_schemas.empty()) {
    prompt << "## Available Tools\n";
    for (const auto& schema : tool_schemas) {
      prompt << "- **" << schema.name << "**: " << schema.description << "\n";
    }
    prompt << "\n";
  }

  prompt << "You are QuantClaw, a helpful AI assistant.";

  return prompt.str();
}

std::string PromptBuilder::get_section(const std::string& filename) const {
  try {
    return memory_manager_->ReadIdentityFile(filename);
  } catch (const std::exception&) {
    return "";
  }
}

std::string PromptBuilder::get_runtime_info() const {
  std::ostringstream info;

  // Current time
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
#ifdef _WIN32
  gmtime_s(&tm, &time_t);
#else
  gmtime_r(&time_t, &tm);
#endif

  info << "- Current time: " << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ")
       << "\n";
  info << "- Workspace: " << memory_manager_->GetWorkspacePath().string()
       << "\n";
  info << "- Platform: "
#ifdef __linux__
       << "linux"
#elif defined(__APPLE__)
       << "darwin"
#elif defined(_WIN32)
       << "win32"
#else
       << "unknown"
#endif
       << "\n";

  return info.str();
}

std::string PromptBuilder::BuildWithComponents(
    const PromptComponents& components,
    const PromptContext& context) const {
  std::ostringstream prompt;

  // 1. SOUL.md - identity (always included)
  auto soul = get_section("SOUL.md");
  if (!soul.empty()) {
    prompt << "## Your Identity\n" << soul << "\n\n";
  }

  // 2. AGENTS.md - behavior instructions (always included)
  auto agents = get_section("AGENTS.md");
  if (!agents.empty()) {
    prompt << "## Agent Behavior\n" << agents << "\n\n";
  }

  // 3. TOOLS.md - tool usage guide (always included)
  auto tools = get_section("TOOLS.md");
  if (!tools.empty()) {
    prompt << "## Tool Usage Guide\n" << tools << "\n\n";
  }

  // 4. Skills protocol (Requirement 1.1)
  if (components.include_skills_protocol) {
    auto skills_protocol = build_skills_protocol();
    if (!skills_protocol.empty()) {
      prompt << "## Skills Protocol\n" << skills_protocol << "\n\n";
    }
  }

  // 5. Memory recall rules (Requirement 1.2)
  if (components.include_memory_rules) {
    auto memory_rules = build_memory_recall_rules();
    if (!memory_rules.empty()) {
      prompt << "## Memory Recall Rules\n" << memory_rules << "\n\n";
    }
  }

  // 6. Channel-specific instructions (Requirement 1.3)
  if (components.include_channel_instructions && !context.channel.empty()) {
    auto channel_instr = build_channel_instructions(context.channel);
    if (!channel_instr.empty()) {
      prompt << "## Channel Instructions\n" << channel_instr << "\n\n";
    }
  }

  // 7. Runtime metadata (Requirement 1.4)
  if (components.include_runtime_metadata) {
    auto runtime_meta = build_runtime_metadata();
    if (!runtime_meta.empty()) {
      prompt << "## Runtime Information\n" << runtime_meta << "\n\n";
    }
  }

  // 8. Sender trust information (Requirement 1.5)
  if (components.include_sender_trust && !context.sender_id.empty()) {
    auto trust_info = build_sender_trust_info(context.sender_id);
    if (!trust_info.empty()) {
      prompt << "## Sender Trust Information\n" << trust_info << "\n\n";
    }
  }

  // 9. Output constraints (Requirement 1.6)
  if (components.include_output_constraints) {
    auto output_constraints = build_output_constraints();
    if (!output_constraints.empty()) {
      prompt << "## Output Constraints\n" << output_constraints << "\n\n";
    }
  }

  // 10. Operation guards (Requirement 1.7)
  if (components.include_operation_guards) {
    auto operation_guards = build_operation_guards();
    if (!operation_guards.empty()) {
      prompt << "## Operation Guards\n" << operation_guards << "\n\n";
    }
  }

  // 11. Available tools (summary or full schema based on count)
  auto tool_schemas = tool_registry_->GetToolSchemas();
  bool use_summary = components.use_tool_summary || 
                     (context.tool_count > 100) || 
                     (tool_schemas.size() > 100);
  
  if (use_summary) {
    auto tool_summary = build_tool_summary();
    if (!tool_summary.empty()) {
      prompt << "## Available Tools (Summary)\n" << tool_summary << "\n\n";
    }
  } else if (!tool_schemas.empty()) {
    prompt << "## Available Tools\n";
    for (const auto& schema : tool_schemas) {
      prompt << "- **" << schema.name << "**: " << schema.description << "\n";
    }
    prompt << "\n";
  }

  // Default identity fallback
  prompt << "You are QuantClaw, a high-performance C++ personal AI assistant. "
         << "Use the available tools when needed to help the user. "
         << "Always be concise and helpful.";

  return prompt.str();
}

std::string PromptBuilder::build_skills_protocol() const {
  // TODO: Implement in Task 1.1.2
  // This should define skill calling format and parameter specifications
  return "";
}

std::string PromptBuilder::build_memory_recall_rules() const {
  // TODO: Implement in Task 1.1.3
  // This should guide when and how to retrieve historical memories
  return "";
}

std::string PromptBuilder::build_channel_instructions(
    const std::string& channel) const {
  // TODO: Implement in Task 1.1.4
  // Return channel-specific instructions if configured
  auto it = channel_instructions_.find(channel);
  if (it != channel_instructions_.end()) {
    return it->second;
  }
  return "";
}

std::string PromptBuilder::build_runtime_metadata() const {
  // TODO: Implement in Task 1.1.5
  // This should include current time, workspace, platform, available tools
  // summary, and session state
  // For now, reuse existing get_runtime_info()
  return get_runtime_info();
}

std::string PromptBuilder::build_sender_trust_info(
    const std::string& sender_id) const {
  // TODO: Implement in Task 1.1.6
  // Return trust level information for the sender
  auto it = sender_trust_.find(sender_id);
  if (it != sender_trust_.end()) {
    std::ostringstream info;
    info << "- Sender trust level: ";
    switch (it->second) {
      case TrustLevel::kTrusted:
        info << "Trusted (verified user/admin)";
        break;
      case TrustLevel::kSemiTrusted:
        info << "Semi-trusted (regular user)";
        break;
      case TrustLevel::kUntrusted:
        info << "Untrusted (unknown/suspicious user)";
        break;
    }
    info << "\n";
    return info.str();
  }
  return "";
}

std::string PromptBuilder::build_output_constraints() const {
  // TODO: Implement in Task 1.1.7
  // This should guide how the assistant formats responses
  return "";
}

std::string PromptBuilder::build_operation_guards() const {
  // TODO: Implement in Task 1.1.8
  // This should define approval processes for dangerous operations
  return "";
}

std::string PromptBuilder::build_tool_summary() const {
  // TODO: Implement in Task 1.1.9
  // When tool count > 100, use summary mode instead of full schemas
  auto tool_schemas = tool_registry_->GetToolSchemas();
  std::ostringstream summary;
  summary << "Available tools (" << tool_schemas.size() << " total):\n";
  for (const auto& schema : tool_schemas) {
    summary << "- " << schema.name << ": " << schema.description << "\n";
  }
  summary << "\nNote: Full tool schemas are available upon request.\n";
  return summary.str();
}

}  // namespace quantclaw
