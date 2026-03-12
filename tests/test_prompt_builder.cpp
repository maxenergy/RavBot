// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

class PromptBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_prompt_test");
    std::filesystem::create_directories(test_dir_ / "skills");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_prompt", null_sink);

    memory_manager_ =
        std::make_shared<quantclaw::MemoryManager>(test_dir_, logger_);
    skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    builder_ = std::make_unique<quantclaw::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);
  }

  void TearDown() override {
    builder_.reset();
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  void write_file(const std::string& name, const std::string& content) {
    std::ofstream f(test_dir_ / name);
    f << content;
    f.close();
  }

  std::filesystem::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
  std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
  std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
  std::unique_ptr<quantclaw::PromptBuilder> builder_;
};

// --- BuildFull tests ---

TEST_F(PromptBuilderTest, BuildFullContainsDefaultIdentity) {
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("You are QuantClaw"), std::string::npos);
  EXPECT_NE(prompt.find("personal AI assistant"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesSoulSection) {
  write_file("SOUL.md", "I am a quantum trading bot.");
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Your Identity"), std::string::npos);
  EXPECT_NE(prompt.find("I am a quantum trading bot."), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesAgentsSection) {
  write_file("AGENTS.md", "Always be concise and direct.");
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Agent Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Always be concise and direct."), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesToolsSection) {
  write_file("TOOLS.md", "Use read for file operations.");
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Tool Usage Guide"), std::string::npos);
  EXPECT_NE(prompt.find("Use read for file operations."), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesMemorySection) {
  write_file("MEMORY.md", "User prefers short answers.");
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Memory"), std::string::npos);
  EXPECT_NE(prompt.find("User prefers short answers."), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesRuntimeInfo) {
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Runtime Information"), std::string::npos);
  EXPECT_NE(prompt.find("Current time:"), std::string::npos);
  EXPECT_NE(prompt.find("Workspace:"), std::string::npos);
  EXPECT_NE(prompt.find("Platform:"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesConversationFocusGuidance) {
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Conversation Focus"), std::string::npos);
  EXPECT_NE(prompt.find("latest message"), std::string::npos);
  EXPECT_NE(prompt.find("clarifying question"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesToolSchemas) {
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Available Tools"), std::string::npos);
  // Built-in tools should be listed
  EXPECT_NE(prompt.find("read"), std::string::npos);
  EXPECT_NE(prompt.find("write"), std::string::npos);
  EXPECT_NE(prompt.find("exec"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullOmitsMissingSections) {
  // No SOUL.md, AGENTS.md, TOOLS.md, MEMORY.md exist
  auto prompt = builder_->BuildFull();
  EXPECT_EQ(prompt.find("## Your Identity"), std::string::npos);
  EXPECT_EQ(prompt.find("## Agent Behavior"), std::string::npos);
  EXPECT_EQ(prompt.find("## Tool Usage Guide"), std::string::npos);
  EXPECT_EQ(prompt.find("## Memory"), std::string::npos);
  // Runtime info and tools should still be present
  EXPECT_NE(prompt.find("## Runtime Information"), std::string::npos);
  EXPECT_NE(prompt.find("## Available Tools"), std::string::npos);
}

// --- BuildMinimal tests ---

TEST_F(PromptBuilderTest, BuildMinimalContainsIdentityFallback) {
  auto prompt = builder_->BuildMinimal();
  EXPECT_NE(prompt.find("You are QuantClaw"), std::string::npos);
  EXPECT_NE(prompt.find("helpful AI assistant"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildMinimalIncludesSoulButNotAgents) {
  write_file("SOUL.md", "I am a quantum bot.");
  write_file("AGENTS.md", "Be verbose.");
  auto prompt = builder_->BuildMinimal();
  EXPECT_NE(prompt.find("I am a quantum bot."), std::string::npos);
  // Minimal should NOT include agents section
  EXPECT_EQ(prompt.find("## Agent Behavior"), std::string::npos);
  EXPECT_EQ(prompt.find("Be verbose."), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildMinimalIncludesTools) {
  auto prompt = builder_->BuildMinimal();
  EXPECT_NE(prompt.find("## Available Tools"), std::string::npos);
  EXPECT_NE(prompt.find("read"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildMinimalNoRuntimeInfo) {
  auto prompt = builder_->BuildMinimal();
  EXPECT_EQ(prompt.find("## Runtime Information"), std::string::npos);
}

// --- BuildFull with all sections populated ---

TEST_F(PromptBuilderTest, BuildFullWithAllSections) {
  write_file("SOUL.md", "SOUL_CONTENT");
  write_file("AGENTS.md", "AGENTS_CONTENT");
  write_file("TOOLS.md", "TOOLS_CONTENT");
  write_file("MEMORY.md", "MEMORY_CONTENT");

  auto prompt = builder_->BuildFull();

  // Verify section order: Identity before Agent Behavior before Tool Usage
  auto soul_pos = prompt.find("## Your Identity");
  auto agents_pos = prompt.find("## Agent Behavior");
  auto tools_guide_pos = prompt.find("## Tool Usage Guide");
  auto memory_pos = prompt.find("## Memory");
  auto runtime_pos = prompt.find("## Runtime Information");
  auto tools_pos = prompt.find("## Available Tools");

  ASSERT_NE(soul_pos, std::string::npos);
  ASSERT_NE(agents_pos, std::string::npos);
  ASSERT_NE(tools_guide_pos, std::string::npos);
  ASSERT_NE(memory_pos, std::string::npos);
  ASSERT_NE(runtime_pos, std::string::npos);
  ASSERT_NE(tools_pos, std::string::npos);

  EXPECT_LT(soul_pos, agents_pos);
  EXPECT_LT(agents_pos, tools_guide_pos);
  EXPECT_LT(tools_guide_pos, memory_pos);
  EXPECT_LT(memory_pos, runtime_pos);
  EXPECT_LT(runtime_pos, tools_pos);
}

// --- No tools registered ---

TEST_F(PromptBuilderTest, BuildFullNoToolsRegistered) {
  // Create a fresh registry without built-in tools
  auto empty_registry = std::make_shared<quantclaw::ToolRegistry>(logger_);
  auto builder =
      quantclaw::PromptBuilder(memory_manager_, skill_loader_, empty_registry);

  auto prompt = builder.BuildFull();
  EXPECT_EQ(prompt.find("## Available Tools"), std::string::npos);
  // Default identity should still be there
  EXPECT_NE(prompt.find("You are QuantClaw"), std::string::npos);
}


// --- BuildWithComponents tests for skills protocol ---

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesSkillsProtocol) {
  // Create a test skill
  std::filesystem::create_directories(test_dir_ / "skills" / "test-skill");
  write_file("skills/test-skill/SKILL.md", R"(---
name: test-skill
emoji: "🧪"
description: A test skill for unit testing
always: true
commands:
  - name: test
    description: Run a test command
    toolName: exec
    argMode: freeform
---

This is a test skill.
)");

  quantclaw::PromptComponents components;
  components.include_skills_protocol = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify skills protocol section is included
  EXPECT_NE(prompt.find("## Skills Protocol"), std::string::npos);
  EXPECT_NE(prompt.find("Skills are specialized capabilities"), std::string::npos);
  EXPECT_NE(prompt.find("### Skill Invocation Format"), std::string::npos);
  EXPECT_NE(prompt.find("### Available Skills"), std::string::npos);
  EXPECT_NE(prompt.find("### Parameter Specifications"), std::string::npos);
  EXPECT_NE(prompt.find("### Best Practices"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsSkillsProtocolIncludesSkillDetails) {
  // Create a test skill with detailed metadata
  std::filesystem::create_directories(test_dir_ / "skills" / "search-skill");
  write_file("skills/search-skill/SKILL.md", R"(---
name: search
emoji: "🔍"
description: Web search with automatic provider fallback
always: true
commands:
  - name: search
    description: Search the web for a query
    toolName: web_search
    argMode: freeform
---

Use the web_search tool to search the web.
)");

  quantclaw::PromptComponents components;
  components.include_skills_protocol = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify skill details are included
  EXPECT_NE(prompt.find("**search**"), std::string::npos);
  EXPECT_NE(prompt.find("🔍"), std::string::npos);
  EXPECT_NE(prompt.find("Web search with automatic provider fallback"), std::string::npos);
  EXPECT_NE(prompt.find("Availability: Always active"), std::string::npos);
  EXPECT_NE(prompt.find("`search`"), std::string::npos);
  EXPECT_NE(prompt.find("Search the web for a query"), std::string::npos);
  EXPECT_NE(prompt.find("uses tool: `web_search`"), std::string::npos);
  EXPECT_NE(prompt.find("Arguments: Freeform text input"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsSkillsProtocolEmptyWhenNoSkills) {
  // No skills directory or empty skills directory
  quantclaw::PromptComponents components;
  components.include_skills_protocol = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Skills protocol should not be included when no skills are available
  EXPECT_EQ(prompt.find("## Skills Protocol"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsSkillsProtocolOmittedWhenDisabled) {
  // Create a test skill
  std::filesystem::create_directories(test_dir_ / "skills" / "test-skill");
  write_file("skills/test-skill/SKILL.md", R"(---
name: test-skill
description: A test skill
always: true
---

Test skill content.
)");

  quantclaw::PromptComponents components;
  components.include_skills_protocol = false;  // Disabled
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Skills protocol should not be included when disabled
  EXPECT_EQ(prompt.find("## Skills Protocol"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsSkillsProtocolHandlesMultipleSkills) {
  // Create multiple test skills
  std::filesystem::create_directories(test_dir_ / "skills" / "skill1");
  write_file("skills/skill1/SKILL.md", R"(---
name: skill1
emoji: "1️⃣"
description: First test skill
always: true
commands:
  - name: cmd1
    description: Command 1
    toolName: tool1
    argMode: structured
---

Skill 1 content.
)");

  std::filesystem::create_directories(test_dir_ / "skills" / "skill2");
  write_file("skills/skill2/SKILL.md", R"(---
name: skill2
emoji: "2️⃣"
description: Second test skill
always: false
commands:
  - name: cmd2
    description: Command 2
    toolName: tool2
    argMode: freeform
---

Skill 2 content.
)");

  quantclaw::PromptComponents components;
  components.include_skills_protocol = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify both skills are included
  EXPECT_NE(prompt.find("**skill1**"), std::string::npos);
  EXPECT_NE(prompt.find("1️⃣"), std::string::npos);
  EXPECT_NE(prompt.find("First test skill"), std::string::npos);
  
  EXPECT_NE(prompt.find("**skill2**"), std::string::npos);
  EXPECT_NE(prompt.find("2️⃣"), std::string::npos);
  EXPECT_NE(prompt.find("Second test skill"), std::string::npos);
  
  // Verify different argument modes are documented
  EXPECT_NE(prompt.find("Arguments: Structured JSON parameters"), std::string::npos);
  EXPECT_NE(prompt.find("Arguments: Freeform text input"), std::string::npos);
}

// --- BuildWithComponents tests for memory recall rules ---

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesMemoryRecallRules) {
  quantclaw::PromptComponents components;
  components.include_memory_rules = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify memory recall rules section is included
  EXPECT_NE(prompt.find("## Memory Recall Rules"), std::string::npos);
  EXPECT_NE(prompt.find("### When to Retrieve Historical Memories"), std::string::npos);
  EXPECT_NE(prompt.find("### How to Retrieve Memories"), std::string::npos);
  EXPECT_NE(prompt.find("### When NOT to Retrieve Memories"), std::string::npos);
  EXPECT_NE(prompt.find("### Memory Recall Best Practices"), std::string::npos);
  EXPECT_NE(prompt.find("### Memory Types and Priority"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsMemoryRecallRulesIncludesTriggerConditions) {
  quantclaw::PromptComponents components;
  components.include_memory_rules = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify trigger conditions are documented
  EXPECT_NE(prompt.find("**Trigger Conditions for Memory Recall:**"), std::string::npos);
  EXPECT_NE(prompt.find("**User References Past Context**"), std::string::npos);
  EXPECT_NE(prompt.find("**Continuation of Previous Work**"), std::string::npos);
  EXPECT_NE(prompt.find("**User Preferences and Patterns**"), std::string::npos);
  EXPECT_NE(prompt.find("**Project-Specific Context**"), std::string::npos);
  EXPECT_NE(prompt.find("**Error Resolution**"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsMemoryRecallRulesIncludesSearchStrategy) {
  quantclaw::PromptComponents components;
  components.include_memory_rules = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify search strategy is documented
  EXPECT_NE(prompt.find("**Memory Search Strategy:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Formulate Specific Queries**"), std::string::npos);
  EXPECT_NE(prompt.find("**Search Scope**"), std::string::npos);
  EXPECT_NE(prompt.find("**Relevance Filtering**"), std::string::npos);
  EXPECT_NE(prompt.find("**Memory Integration**"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsMemoryRecallRulesIncludesBestPractices) {
  quantclaw::PromptComponents components;
  components.include_memory_rules = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify best practices are documented
  EXPECT_NE(prompt.find("**Be Transparent**"), std::string::npos);
  EXPECT_NE(prompt.find("**Verify Relevance**"), std::string::npos);
  EXPECT_NE(prompt.find("**Respect Privacy**"), std::string::npos);
  EXPECT_NE(prompt.find("**Update Context**"), std::string::npos);
  EXPECT_NE(prompt.find("**Graceful Degradation**"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsMemoryRecallRulesIncludesPriority) {
  quantclaw::PromptComponents components;
  components.include_memory_rules = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify memory priority is documented
  EXPECT_NE(prompt.find("**Explicit User Instructions**"), std::string::npos);
  EXPECT_NE(prompt.find("**Project-Specific Decisions**"), std::string::npos);
  EXPECT_NE(prompt.find("**Recent Context**"), std::string::npos);
  EXPECT_NE(prompt.find("**User Preferences**"), std::string::npos);
  EXPECT_NE(prompt.find("**Historical Reference**"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsMemoryRecallRulesOmittedWhenDisabled) {
  quantclaw::PromptComponents components;
  components.include_memory_rules = false;  // Disabled
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Memory recall rules should not be included when disabled
  EXPECT_EQ(prompt.find("## Memory Recall Rules"), std::string::npos);
  EXPECT_EQ(prompt.find("### When to Retrieve Historical Memories"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsMemoryRecallRulesIncludesExamples) {
  quantclaw::PromptComponents components;
  components.include_memory_rules = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify examples are included
  EXPECT_NE(prompt.find("Example: \"What did we discuss about the API design?\""), std::string::npos);
  EXPECT_NE(prompt.find("Example: \"Let's continue working on that feature\""), std::string::npos);
  EXPECT_NE(prompt.find("Example: \"How should I structure this component?\""), std::string::npos);
  EXPECT_NE(prompt.find("Example: \"Add a new endpoint to the API\""), std::string::npos);
  EXPECT_NE(prompt.find("Example: \"This error keeps happening\""), std::string::npos);
}

// --- BuildWithComponents tests for channel instructions ---

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesTelegramChannelInstructions) {
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  quantclaw::PromptContext context;
  context.channel = "telegram";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Telegram channel instructions are included
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### Telegram Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("**Message Formatting:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Reply Modes:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Interaction Patterns:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Context Management:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Error Handling:**"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsTelegramInstructionsIncludeFormattingGuidance) {
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  quantclaw::PromptContext context;
  context.channel = "telegram";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Telegram-specific formatting guidance
  EXPECT_NE(prompt.find("Telegram supports Markdown formatting"), std::string::npos);
  EXPECT_NE(prompt.find("max 4096 chars per message"), std::string::npos);
  EXPECT_NE(prompt.find("Keep messages concise and readable on mobile devices"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsTelegramInstructionsIncludeReplyModes) {
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  quantclaw::PromptContext context;
  context.channel = "telegram";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Telegram reply mode guidance
  EXPECT_NE(prompt.find("In private chats: respond directly"), std::string::npos);
  EXPECT_NE(prompt.find("In group chats: use reply-to-message"), std::string::npos);
  EXPECT_NE(prompt.find("In topics/threads: stay within the thread context"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesDiscordChannelInstructions) {
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  quantclaw::PromptContext context;
  context.channel = "discord";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Discord channel instructions are included
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### Discord Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Maximum message length is 2000 characters"), std::string::npos);
  EXPECT_NE(prompt.find("Use @mentions to address specific users"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesSlackChannelInstructions) {
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  quantclaw::PromptContext context;
  context.channel = "slack";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Slack channel instructions are included
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### Slack Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Use Slack's mrkdwn format"), std::string::npos);
  EXPECT_NE(prompt.find("Keep messages professional and workplace-appropriate"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesCLIChannelInstructions) {
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  quantclaw::PromptContext context;
  context.channel = "cli";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify CLI channel instructions are included
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### CLI/Terminal Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Use plain text or ANSI color codes"), std::string::npos);
  EXPECT_NE(prompt.find("Include command examples that can be copy-pasted"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesWebUIChannelInstructions) {
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  quantclaw::PromptContext context;
  context.channel = "web";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Web UI channel instructions are included
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### Web UI Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Use Markdown for rich formatting"), std::string::npos);
  EXPECT_NE(prompt.find("Structure responses with headers and lists"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesGenericChannelInstructions) {
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  quantclaw::PromptContext context;
  context.channel = "unknown_channel";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify generic channel instructions are included for unknown channels
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### General Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Use clear, concise language"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsChannelInstructionsOmittedWhenDisabled) {
  quantclaw::PromptComponents components;
  components.include_channel_instructions = false;  // Disabled
  quantclaw::PromptContext context;
  context.channel = "telegram";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Channel instructions should not be included when disabled
  EXPECT_EQ(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_EQ(prompt.find("### Telegram Channel Behavior"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsChannelInstructionsOmittedWhenNoChannel) {
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  quantclaw::PromptContext context;
  // No channel specified
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Channel instructions should not be included when no channel is specified
  EXPECT_EQ(prompt.find("## Channel Instructions"), std::string::npos);
}

TEST_F(PromptBuilderTest, SetChannelInstructionsCustomOverride) {
  // Set custom channel instructions
  builder_->SetChannelInstructions("telegram", "Custom Telegram instructions for testing.");
  
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  quantclaw::PromptContext context;
  context.channel = "telegram";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify custom instructions are used instead of default
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("Custom Telegram instructions for testing."), std::string::npos);
  // Default instructions should not be present
  EXPECT_EQ(prompt.find("### Telegram Channel Behavior"), std::string::npos);
}

TEST_F(PromptBuilderTest, SetChannelInstructionsMultipleChannels) {
  // Set custom instructions for multiple channels
  builder_->SetChannelInstructions("telegram", "Telegram custom");
  builder_->SetChannelInstructions("discord", "Discord custom");
  
  quantclaw::PromptComponents components;
  components.include_channel_instructions = true;
  
  // Test Telegram
  quantclaw::PromptContext telegram_context;
  telegram_context.channel = "telegram";
  auto telegram_prompt = builder_->BuildWithComponents(components, telegram_context);
  EXPECT_NE(telegram_prompt.find("Telegram custom"), std::string::npos);
  EXPECT_EQ(telegram_prompt.find("Discord custom"), std::string::npos);
  
  // Test Discord
  quantclaw::PromptContext discord_context;
  discord_context.channel = "discord";
  auto discord_prompt = builder_->BuildWithComponents(components, discord_context);
  EXPECT_NE(discord_prompt.find("Discord custom"), std::string::npos);
  EXPECT_EQ(discord_prompt.find("Telegram custom"), std::string::npos);
}

// --- BuildWithComponents tests for runtime metadata (Task 1.1.5) ---

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesRuntimeMetadata) {
  quantclaw::PromptComponents components;
  components.include_runtime_metadata = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify runtime metadata section is included
  EXPECT_NE(prompt.find("## Runtime Information"), std::string::npos);
  EXPECT_NE(prompt.find("**Current time**:"), std::string::npos);
  EXPECT_NE(prompt.find("**Workspace**:"), std::string::npos);
  EXPECT_NE(prompt.find("**Platform**:"), std::string::npos);
  EXPECT_NE(prompt.find("**Available tools**:"), std::string::npos);
  EXPECT_NE(prompt.find("**System capabilities**:"), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesCurrentTime) {
  quantclaw::PromptComponents components;
  components.include_runtime_metadata = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify time is in ISO 8601 format (YYYY-MM-DDTHH:MM:SSZ)
  EXPECT_NE(prompt.find("**Current time**:"), std::string::npos);
  // Should contain a timestamp pattern
  EXPECT_TRUE(prompt.find("T") != std::string::npos && 
              prompt.find("Z") != std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesWorkspacePath) {
  quantclaw::PromptComponents components;
  components.include_runtime_metadata = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify workspace path is included
  EXPECT_NE(prompt.find("**Workspace**:"), std::string::npos);
  EXPECT_NE(prompt.find(test_dir_.string()), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesPlatformInfo) {
  quantclaw::PromptComponents components;
  components.include_runtime_metadata = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify platform is included
  EXPECT_NE(prompt.find("**Platform**:"), std::string::npos);
  // Should contain one of: Linux, macOS, Windows, Unknown
  bool has_platform = (prompt.find("Linux") != std::string::npos) ||
                      (prompt.find("macOS") != std::string::npos) ||
                      (prompt.find("Windows") != std::string::npos) ||
                      (prompt.find("Unknown") != std::string::npos);
  EXPECT_TRUE(has_platform);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesToolsSummary) {
  quantclaw::PromptComponents components;
  components.include_runtime_metadata = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify tools summary is included
  EXPECT_NE(prompt.find("**Available tools**:"), std::string::npos);
  EXPECT_NE(prompt.find("total"), std::string::npos);
  
  // Should include category breakdown
  EXPECT_NE(prompt.find("Categories:"), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesToolCategories) {
  quantclaw::PromptComponents components;
  components.include_runtime_metadata = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify tool categories are present (based on built-in tools)
  EXPECT_NE(prompt.find("Categories:"), std::string::npos);
  // Should have file, execution, web, memory, or general categories
  bool has_categories = (prompt.find("file") != std::string::npos) ||
                        (prompt.find("execution") != std::string::npos) ||
                        (prompt.find("web") != std::string::npos) ||
                        (prompt.find("memory") != std::string::npos) ||
                        (prompt.find("general") != std::string::npos);
  EXPECT_TRUE(has_categories);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesSystemCapabilities) {
  quantclaw::PromptComponents components;
  components.include_runtime_metadata = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify system capabilities are documented
  EXPECT_NE(prompt.find("**System capabilities**:"), std::string::npos);
  EXPECT_NE(prompt.find("Multi-turn conversation support"), std::string::npos);
  EXPECT_NE(prompt.find("Tool execution and chaining"), std::string::npos);
  EXPECT_NE(prompt.find("Memory persistence and retrieval"), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesSkillsCount) {
  // Create a test skill
  std::filesystem::create_directories(test_dir_ / "skills" / "test-skill");
  write_file("skills/test-skill/SKILL.md", R"(---
name: test-skill
description: A test skill
always: true
---

Test skill content.
)");

  quantclaw::PromptComponents components;
  components.include_runtime_metadata = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify skills count is included
  EXPECT_NE(prompt.find("skill(s) loaded"), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesPerformanceHints) {
  quantclaw::PromptComponents components;
  components.include_runtime_metadata = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify performance hints are included
  EXPECT_NE(prompt.find("**Performance notes**:"), std::string::npos);
  EXPECT_NE(prompt.find("Context window management active"), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataLargeToolSetHint) {
  // Create a registry with many tools to trigger the large tool set hint
  auto large_registry = std::make_shared<quantclaw::ToolRegistry>(logger_);
  large_registry->RegisterBuiltinTools();
  
  // Add dummy tools to exceed 100
  for (int i = 0; i < 110; ++i) {
    std::string tool_name = "dummy_tool_" + std::to_string(i);
    std::string description = "Dummy tool for testing";
    nlohmann::json params_schema = nlohmann::json::object();
    
    large_registry->RegisterExternalTool(
        tool_name,
        description,
        params_schema,
        [](const nlohmann::json&) { return "dummy result"; });
  }
  
  auto builder = quantclaw::PromptBuilder(
      memory_manager_, skill_loader_, large_registry);
  
  quantclaw::PromptComponents components;
  components.include_runtime_metadata = true;
  quantclaw::PromptContext context;
  
  auto prompt = builder.BuildWithComponents(components, context);
  
  // Verify large tool set hint is included
  EXPECT_NE(prompt.find("Large tool set detected"), std::string::npos);
  EXPECT_NE(prompt.find("using tool summary mode"), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataOmittedWhenDisabled) {
  quantclaw::PromptComponents components;
  components.include_runtime_metadata = false;  // Disabled
  quantclaw::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Runtime metadata should not be included when disabled
  // Note: The section header might still appear from other components,
  // but the detailed metadata should not be present
  EXPECT_EQ(prompt.find("**Current time**:"), std::string::npos);
  EXPECT_EQ(prompt.find("**Available tools**:"), std::string::npos);
  EXPECT_EQ(prompt.find("**System capabilities**:"), std::string::npos);
}
