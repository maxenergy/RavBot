// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "ravbot/core/memory_manager.hpp"
#include "ravbot/core/prompt_builder.hpp"
#include "ravbot/core/skill_loader.hpp"
#include "ravbot/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

class PromptBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = ravbot::test::MakeTestDir("ravbot_prompt_test");
    std::filesystem::create_directories(test_dir_ / "skills");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_prompt", null_sink);

    memory_manager_ =
        std::make_shared<ravbot::MemoryManager>(test_dir_, logger_);
    skill_loader_ = std::make_shared<ravbot::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<ravbot::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    builder_ = std::make_unique<ravbot::PromptBuilder>(
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
  std::shared_ptr<ravbot::MemoryManager> memory_manager_;
  std::shared_ptr<ravbot::SkillLoader> skill_loader_;
  std::shared_ptr<ravbot::ToolRegistry> tool_registry_;
  std::unique_ptr<ravbot::PromptBuilder> builder_;
};

// --- BuildFull tests ---

TEST_F(PromptBuilderTest, BuildFullContainsDefaultIdentity) {
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("You are RavBot"), std::string::npos);
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
  EXPECT_NE(prompt.find("You are RavBot"), std::string::npos);
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
  auto empty_registry = std::make_shared<ravbot::ToolRegistry>(logger_);
  auto builder =
      ravbot::PromptBuilder(memory_manager_, skill_loader_, empty_registry);

  auto prompt = builder.BuildFull();
  EXPECT_EQ(prompt.find("## Available Tools"), std::string::npos);
  // Default identity should still be there
  EXPECT_NE(prompt.find("You are RavBot"), std::string::npos);
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

  ravbot::PromptComponents components;
  components.include_skills_protocol = true;
  ravbot::PromptContext context;
  
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

  ravbot::PromptComponents components;
  components.include_skills_protocol = true;
  ravbot::PromptContext context;
  
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
  ravbot::PromptComponents components;
  components.include_skills_protocol = true;
  ravbot::PromptContext context;
  
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

  ravbot::PromptComponents components;
  components.include_skills_protocol = false;  // Disabled
  ravbot::PromptContext context;
  
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

  ravbot::PromptComponents components;
  components.include_skills_protocol = true;
  ravbot::PromptContext context;
  
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
  ravbot::PromptComponents components;
  components.include_memory_rules = true;
  ravbot::PromptContext context;
  
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
  ravbot::PromptComponents components;
  components.include_memory_rules = true;
  ravbot::PromptContext context;
  
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
  ravbot::PromptComponents components;
  components.include_memory_rules = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify search strategy is documented
  EXPECT_NE(prompt.find("**Memory Search Strategy:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Formulate Specific Queries**"), std::string::npos);
  EXPECT_NE(prompt.find("**Search Scope**"), std::string::npos);
  EXPECT_NE(prompt.find("**Relevance Filtering**"), std::string::npos);
  EXPECT_NE(prompt.find("**Memory Integration**"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsMemoryRecallRulesIncludesBestPractices) {
  ravbot::PromptComponents components;
  components.include_memory_rules = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify best practices are documented
  EXPECT_NE(prompt.find("**Be Transparent**"), std::string::npos);
  EXPECT_NE(prompt.find("**Verify Relevance**"), std::string::npos);
  EXPECT_NE(prompt.find("**Respect Privacy**"), std::string::npos);
  EXPECT_NE(prompt.find("**Update Context**"), std::string::npos);
  EXPECT_NE(prompt.find("**Graceful Degradation**"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsMemoryRecallRulesIncludesPriority) {
  ravbot::PromptComponents components;
  components.include_memory_rules = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify memory priority is documented
  EXPECT_NE(prompt.find("**Explicit User Instructions**"), std::string::npos);
  EXPECT_NE(prompt.find("**Project-Specific Decisions**"), std::string::npos);
  EXPECT_NE(prompt.find("**Recent Context**"), std::string::npos);
  EXPECT_NE(prompt.find("**User Preferences**"), std::string::npos);
  EXPECT_NE(prompt.find("**Historical Reference**"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsMemoryRecallRulesOmittedWhenDisabled) {
  ravbot::PromptComponents components;
  components.include_memory_rules = false;  // Disabled
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Memory recall rules should not be included when disabled
  EXPECT_EQ(prompt.find("## Memory Recall Rules"), std::string::npos);
  EXPECT_EQ(prompt.find("### When to Retrieve Historical Memories"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsMemoryRecallRulesIncludesExamples) {
  ravbot::PromptComponents components;
  components.include_memory_rules = true;
  ravbot::PromptContext context;
  
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
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  ravbot::PromptContext context;
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
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  ravbot::PromptContext context;
  context.channel = "telegram";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Telegram-specific formatting guidance
  EXPECT_NE(prompt.find("Telegram supports Markdown formatting"), std::string::npos);
  EXPECT_NE(prompt.find("max 4096 chars per message"), std::string::npos);
  EXPECT_NE(prompt.find("Keep messages concise and readable on mobile devices"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsTelegramInstructionsIncludeReplyModes) {
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  ravbot::PromptContext context;
  context.channel = "telegram";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Telegram reply mode guidance
  EXPECT_NE(prompt.find("In private chats: respond directly"), std::string::npos);
  EXPECT_NE(prompt.find("In group chats: use reply-to-message"), std::string::npos);
  EXPECT_NE(prompt.find("In topics/threads: stay within the thread context"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesDiscordChannelInstructions) {
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  ravbot::PromptContext context;
  context.channel = "discord";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Discord channel instructions are included
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### Discord Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Maximum message length is 2000 characters"), std::string::npos);
  EXPECT_NE(prompt.find("Use @mentions to address specific users"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesSlackChannelInstructions) {
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  ravbot::PromptContext context;
  context.channel = "slack";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Slack channel instructions are included
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### Slack Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Use Slack's mrkdwn format"), std::string::npos);
  EXPECT_NE(prompt.find("Keep messages professional and workplace-appropriate"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesCLIChannelInstructions) {
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  ravbot::PromptContext context;
  context.channel = "cli";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify CLI channel instructions are included
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### CLI/Terminal Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Use plain text or ANSI color codes"), std::string::npos);
  EXPECT_NE(prompt.find("Include command examples that can be copy-pasted"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesWebUIChannelInstructions) {
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  ravbot::PromptContext context;
  context.channel = "web";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify Web UI channel instructions are included
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### Web UI Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Use Markdown for rich formatting"), std::string::npos);
  EXPECT_NE(prompt.find("Structure responses with headers and lists"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesGenericChannelInstructions) {
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  ravbot::PromptContext context;
  context.channel = "unknown_channel";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify generic channel instructions are included for unknown channels
  EXPECT_NE(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_NE(prompt.find("### General Channel Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Use clear, concise language"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsChannelInstructionsOmittedWhenDisabled) {
  ravbot::PromptComponents components;
  components.include_channel_instructions = false;  // Disabled
  ravbot::PromptContext context;
  context.channel = "telegram";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Channel instructions should not be included when disabled
  EXPECT_EQ(prompt.find("## Channel Instructions"), std::string::npos);
  EXPECT_EQ(prompt.find("### Telegram Channel Behavior"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildWithComponentsChannelInstructionsOmittedWhenNoChannel) {
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  ravbot::PromptContext context;
  // No channel specified
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Channel instructions should not be included when no channel is specified
  EXPECT_EQ(prompt.find("## Channel Instructions"), std::string::npos);
}

TEST_F(PromptBuilderTest, SetChannelInstructionsCustomOverride) {
  // Set custom channel instructions
  builder_->SetChannelInstructions("telegram", "Custom Telegram instructions for testing.");
  
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  ravbot::PromptContext context;
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
  
  ravbot::PromptComponents components;
  components.include_channel_instructions = true;
  
  // Test Telegram
  ravbot::PromptContext telegram_context;
  telegram_context.channel = "telegram";
  auto telegram_prompt = builder_->BuildWithComponents(components, telegram_context);
  EXPECT_NE(telegram_prompt.find("Telegram custom"), std::string::npos);
  EXPECT_EQ(telegram_prompt.find("Discord custom"), std::string::npos);
  
  // Test Discord
  ravbot::PromptContext discord_context;
  discord_context.channel = "discord";
  auto discord_prompt = builder_->BuildWithComponents(components, discord_context);
  EXPECT_NE(discord_prompt.find("Discord custom"), std::string::npos);
  EXPECT_EQ(discord_prompt.find("Telegram custom"), std::string::npos);
}

// --- BuildWithComponents tests for runtime metadata (Task 1.1.5) ---

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesRuntimeMetadata) {
  ravbot::PromptComponents components;
  components.include_runtime_metadata = true;
  ravbot::PromptContext context;
  
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
  ravbot::PromptComponents components;
  components.include_runtime_metadata = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify time is in ISO 8601 format (YYYY-MM-DDTHH:MM:SSZ)
  EXPECT_NE(prompt.find("**Current time**:"), std::string::npos);
  // Should contain a timestamp pattern
  EXPECT_TRUE(prompt.find("T") != std::string::npos && 
              prompt.find("Z") != std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesWorkspacePath) {
  ravbot::PromptComponents components;
  components.include_runtime_metadata = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify workspace path is included
  EXPECT_NE(prompt.find("**Workspace**:"), std::string::npos);
  EXPECT_NE(prompt.find(test_dir_.string()), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesPlatformInfo) {
  ravbot::PromptComponents components;
  components.include_runtime_metadata = true;
  ravbot::PromptContext context;
  
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
  ravbot::PromptComponents components;
  components.include_runtime_metadata = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify tools summary is included
  EXPECT_NE(prompt.find("**Available tools**:"), std::string::npos);
  EXPECT_NE(prompt.find("total"), std::string::npos);
  
  // Should include category breakdown
  EXPECT_NE(prompt.find("Categories:"), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesToolCategories) {
  ravbot::PromptComponents components;
  components.include_runtime_metadata = true;
  ravbot::PromptContext context;
  
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
  ravbot::PromptComponents components;
  components.include_runtime_metadata = true;
  ravbot::PromptContext context;
  
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

  ravbot::PromptComponents components;
  components.include_runtime_metadata = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify skills count is included
  EXPECT_NE(prompt.find("skill(s) loaded"), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataIncludesPerformanceHints) {
  ravbot::PromptComponents components;
  components.include_runtime_metadata = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify performance hints are included
  EXPECT_NE(prompt.find("**Performance notes**:"), std::string::npos);
  EXPECT_NE(prompt.find("Context window management active"), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataLargeToolSetHint) {
  // Create a registry with many tools to trigger the large tool set hint
  auto large_registry = std::make_shared<ravbot::ToolRegistry>(logger_);
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
  
  auto builder = ravbot::PromptBuilder(
      memory_manager_, skill_loader_, large_registry);
  
  ravbot::PromptComponents components;
  components.include_runtime_metadata = true;
  ravbot::PromptContext context;
  
  auto prompt = builder.BuildWithComponents(components, context);
  
  // Verify large tool set hint is included
  EXPECT_NE(prompt.find("Large tool set detected"), std::string::npos);
  EXPECT_NE(prompt.find("using tool summary mode"), std::string::npos);
}

TEST_F(PromptBuilderTest, RuntimeMetadataOmittedWhenDisabled) {
  ravbot::PromptComponents components;
  components.include_runtime_metadata = false;  // Disabled
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Runtime metadata should not be included when disabled
  // Note: The section header might still appear from other components,
  // but the detailed metadata should not be present
  EXPECT_EQ(prompt.find("**Current time**:"), std::string::npos);
  EXPECT_EQ(prompt.find("**Available tools**:"), std::string::npos);
  EXPECT_EQ(prompt.find("**System capabilities**:"), std::string::npos);
}

// --- BuildWithComponents tests for sender trust info (Task 1.1.6) ---

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesSenderTrustInfo) {
  // Set sender trust level
  builder_->SetSenderTrust("user123", ravbot::TrustLevel::kSemiTrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "user123";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify sender trust information section is included
  EXPECT_NE(prompt.find("## Sender Trust Information"), std::string::npos);
  EXPECT_NE(prompt.find("### Sender Trust Level"), std::string::npos);
  EXPECT_NE(prompt.find("**Current sender**: `user123`"), std::string::npos);
  EXPECT_NE(prompt.find("### Trust Model Overview"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoTrustedUserPermissions) {
  builder_->SetSenderTrust("admin_user", ravbot::TrustLevel::kTrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "admin_user";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify trusted user information
  EXPECT_NE(prompt.find("**Trusted** (verified user/administrator)"), std::string::npos);
  EXPECT_NE(prompt.find("**Permissions and Behavior:**"), std::string::npos);
  EXPECT_NE(prompt.find("elevated privileges"), std::string::npos);
  EXPECT_NE(prompt.find("execute sensitive operations"), std::string::npos);
  EXPECT_NE(prompt.find("Administrative tools and privileged actions are available"), std::string::npos);
  EXPECT_NE(prompt.find("**Security Considerations:**"), std::string::npos);
  EXPECT_NE(prompt.find("validate dangerous operations for safety"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoSemiTrustedUserPermissions) {
  builder_->SetSenderTrust("regular_user", ravbot::TrustLevel::kSemiTrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "regular_user";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify semi-trusted user information
  EXPECT_NE(prompt.find("**Semi-trusted** (regular authenticated user)"), std::string::npos);
  EXPECT_NE(prompt.find("authenticated but not verified as an administrator"), std::string::npos);
  EXPECT_NE(prompt.find("Standard operations are permitted"), std::string::npos);
  EXPECT_NE(prompt.find("Sensitive operations may require confirmation"), std::string::npos);
  EXPECT_NE(prompt.find("**Operational Guidelines:**"), std::string::npos);
  EXPECT_NE(prompt.find("Ask for confirmation on destructive operations"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoUntrustedUserRestrictions) {
  builder_->SetSenderTrust("unknown_user", ravbot::TrustLevel::kUntrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "unknown_user";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify untrusted user information
  EXPECT_NE(prompt.find("**Untrusted** (unknown or suspicious sender)"), std::string::npos);
  EXPECT_NE(prompt.find("Apply maximum security restrictions"), std::string::npos);
  EXPECT_NE(prompt.find("**CRITICAL**: Do not execute system commands"), std::string::npos);
  EXPECT_NE(prompt.find("Restrict to information retrieval"), std::string::npos);
  EXPECT_NE(prompt.find("Do not reveal system paths"), std::string::npos);
  EXPECT_NE(prompt.find("Decline requests for privileged operations"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoDefaultsToSemiTrusted) {
  // Don't set trust level explicitly
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "new_user";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Should default to semi-trusted
  EXPECT_NE(prompt.find("**Semi-trusted** (regular authenticated user)"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoIncludesTrustModelOverview) {
  builder_->SetSenderTrust("user", ravbot::TrustLevel::kTrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "user";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify trust model overview is included
  EXPECT_NE(prompt.find("### Trust Model Overview"), std::string::npos);
  EXPECT_NE(prompt.find("**Trusted**: Verified administrators"), std::string::npos);
  EXPECT_NE(prompt.find("**Semi-trusted**: Authenticated regular users"), std::string::npos);
  EXPECT_NE(prompt.find("**Untrusted**: Unauthenticated or suspicious users"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoIncludesContentSourceTrust) {
  builder_->SetSenderTrust("user", ravbot::TrustLevel::kTrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "user";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify content source trust information
  EXPECT_NE(prompt.find("**Content Source Trust:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Local files**: Trusted"), std::string::npos);
  EXPECT_NE(prompt.find("**User input**: Semi-trusted"), std::string::npos);
  EXPECT_NE(prompt.find("**Web search/fetch**: Untrusted"), std::string::npos);
  EXPECT_NE(prompt.find("potential prompt injection risk"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoIncludesBestPractices) {
  builder_->SetSenderTrust("user", ravbot::TrustLevel::kTrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "user";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify best practices are included
  EXPECT_NE(prompt.find("**Best Practices:**"), std::string::npos);
  EXPECT_NE(prompt.find("Always apply appropriate security measures"), std::string::npos);
  EXPECT_NE(prompt.find("Validate inputs regardless of trust level"), std::string::npos);
  EXPECT_NE(prompt.find("Log security-relevant operations"), std::string::npos);
  EXPECT_NE(prompt.find("When in doubt, err on the side of caution"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoOmittedWhenDisabled) {
  builder_->SetSenderTrust("user", ravbot::TrustLevel::kTrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = false;  // Disabled
  ravbot::PromptContext context;
  context.sender_id = "user";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Sender trust information should not be included when disabled
  EXPECT_EQ(prompt.find("## Sender Trust Information"), std::string::npos);
  EXPECT_EQ(prompt.find("### Sender Trust Level"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoOmittedWhenNoSenderId) {
  builder_->SetSenderTrust("user", ravbot::TrustLevel::kTrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  // No sender_id specified
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Sender trust information should not be included when no sender_id
  EXPECT_EQ(prompt.find("## Sender Trust Information"), std::string::npos);
}

TEST_F(PromptBuilderTest, SetSenderTrustMultipleUsers) {
  // Set trust levels for multiple users
  builder_->SetSenderTrust("admin", ravbot::TrustLevel::kTrusted);
  builder_->SetSenderTrust("user", ravbot::TrustLevel::kSemiTrusted);
  builder_->SetSenderTrust("guest", ravbot::TrustLevel::kUntrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  
  // Test admin - should show Trusted status
  ravbot::PromptContext admin_context;
  admin_context.sender_id = "admin";
  auto admin_prompt = builder_->BuildWithComponents(components, admin_context);
  EXPECT_NE(admin_prompt.find("**Trust level**: **Trusted** (verified user/administrator)"), std::string::npos);
  EXPECT_EQ(admin_prompt.find("**Trust level**: **Semi-trusted**"), std::string::npos);
  EXPECT_EQ(admin_prompt.find("**Trust level**: **Untrusted**"), std::string::npos);
  
  // Test user - should show Semi-trusted status
  ravbot::PromptContext user_context;
  user_context.sender_id = "user";
  auto user_prompt = builder_->BuildWithComponents(components, user_context);
  EXPECT_NE(user_prompt.find("**Trust level**: **Semi-trusted** (regular authenticated user)"), std::string::npos);
  EXPECT_EQ(user_prompt.find("**Trust level**: **Trusted** (verified user/administrator)"), std::string::npos);
  EXPECT_EQ(user_prompt.find("**Trust level**: **Untrusted**"), std::string::npos);
  
  // Test guest - should show Untrusted status
  ravbot::PromptContext guest_context;
  guest_context.sender_id = "guest";
  auto guest_prompt = builder_->BuildWithComponents(components, guest_context);
  EXPECT_NE(guest_prompt.find("**Trust level**: **Untrusted** (unknown or suspicious sender)"), std::string::npos);
  EXPECT_EQ(guest_prompt.find("**Trust level**: **Trusted** (verified user/administrator)"), std::string::npos);
  EXPECT_EQ(guest_prompt.find("**Trust level**: **Semi-trusted** (regular authenticated user)"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoTrustedIncludesSecurityConsiderations) {
  builder_->SetSenderTrust("admin", ravbot::TrustLevel::kTrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "admin";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify security considerations for trusted users
  EXPECT_NE(prompt.find("Still validate dangerous operations for safety"), std::string::npos);
  EXPECT_NE(prompt.find("recursive deletes, system modifications"), std::string::npos);
  EXPECT_NE(prompt.find("Provide clear explanations of what actions will be performed"), std::string::npos);
  EXPECT_NE(prompt.find("Log all privileged operations for audit purposes"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoSemiTrustedIncludesOperationalGuidelines) {
  builder_->SetSenderTrust("user", ravbot::TrustLevel::kSemiTrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "user";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify operational guidelines for semi-trusted users
  EXPECT_NE(prompt.find("Provide clear explanations of actions before execution"), std::string::npos);
  EXPECT_NE(prompt.find("Limit scope of file operations to workspace when possible"), std::string::npos);
  EXPECT_NE(prompt.find("Monitor resource usage and apply reasonable limits"), std::string::npos);
}

TEST_F(PromptBuilderTest, SenderTrustInfoUntrustedIncludesCriticalWarnings) {
  builder_->SetSenderTrust("suspicious", ravbot::TrustLevel::kUntrusted);
  
  ravbot::PromptComponents components;
  components.include_sender_trust = true;
  ravbot::PromptContext context;
  context.sender_id = "suspicious";
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify critical warnings for untrusted users
  EXPECT_NE(prompt.find("**CRITICAL**"), std::string::npos);
  EXPECT_NE(prompt.find("Do not execute system commands or file modifications"), std::string::npos);
  EXPECT_NE(prompt.find("Sanitize all outputs to prevent information leakage"), std::string::npos);
  EXPECT_NE(prompt.find("Log all interactions for security audit"), std::string::npos);
  EXPECT_NE(prompt.find("Report suspicious behavior patterns to administrators"), std::string::npos);
}

// --- BuildWithComponents tests for output constraints (Task 1.1.7) ---

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesOutputConstraints) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify output constraints section is included
  EXPECT_NE(prompt.find("## Output Constraints"), std::string::npos);
  EXPECT_NE(prompt.find("### Response Formatting Guidelines"), std::string::npos);
  EXPECT_NE(prompt.find("**General Principles:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Code and Technical Content:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Lists and Structure:**"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesGeneralPrinciples) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify general principles are documented
  EXPECT_NE(prompt.find("Be concise and direct"), std::string::npos);
  EXPECT_NE(prompt.find("Structure responses with clear sections"), std::string::npos);
  EXPECT_NE(prompt.find("Use markdown formatting for readability"), std::string::npos);
  EXPECT_NE(prompt.find("Prioritize actionable information"), std::string::npos);
  EXPECT_NE(prompt.find("Adapt your response style to the communication channel"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesCodeFormattingGuidance) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify code formatting guidance
  EXPECT_NE(prompt.find("Use code blocks with language syntax highlighting"), std::string::npos);
  EXPECT_NE(prompt.find("Include inline code formatting for commands"), std::string::npos);
  EXPECT_NE(prompt.find("Provide complete, working examples when possible"), std::string::npos);
  EXPECT_NE(prompt.find("Add brief comments to explain non-obvious code sections"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesListStructureGuidance) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify list and structure guidance
  EXPECT_NE(prompt.find("Use bullet points for unordered items"), std::string::npos);
  EXPECT_NE(prompt.find("Use numbered lists for sequential steps"), std::string::npos);
  EXPECT_NE(prompt.find("Keep list items concise"), std::string::npos);
  EXPECT_NE(prompt.find("Group related items together"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesEmphasisGuidance) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify emphasis and highlighting guidance
  EXPECT_NE(prompt.find("**Emphasis and Highlighting:**"), std::string::npos);
  EXPECT_NE(prompt.find("Use **bold** for important terms"), std::string::npos);
  EXPECT_NE(prompt.find("Use *italic* for subtle emphasis"), std::string::npos);
  EXPECT_NE(prompt.find("Use `code formatting` for commands"), std::string::npos);
  EXPECT_NE(prompt.find("Avoid excessive formatting"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesErrorMessageGuidance) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify error message guidance
  EXPECT_NE(prompt.find("**Error Messages and Warnings:**"), std::string::npos);
  EXPECT_NE(prompt.find("Clearly state what went wrong"), std::string::npos);
  EXPECT_NE(prompt.find("Explain the likely cause when known"), std::string::npos);
  EXPECT_NE(prompt.find("Provide specific remediation steps"), std::string::npos);
  EXPECT_NE(prompt.find("Suggest alternatives if the requested action cannot be completed"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesToolUsageFeedback) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify tool usage feedback guidance
  EXPECT_NE(prompt.find("**Tool Usage Feedback:**"), std::string::npos);
  EXPECT_NE(prompt.find("Briefly mention which tools you're using"), std::string::npos);
  EXPECT_NE(prompt.find("Explain why you're using a particular tool"), std::string::npos);
  EXPECT_NE(prompt.find("Summarize tool results rather than dumping raw output"), std::string::npos);
  EXPECT_NE(prompt.find("Highlight key findings from tool execution"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesLengthVerbosityGuidance) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify length and verbosity guidance
  EXPECT_NE(prompt.find("**Length and Verbosity:**"), std::string::npos);
  EXPECT_NE(prompt.find("Keep responses focused on the user's immediate question"), std::string::npos);
  EXPECT_NE(prompt.find("Avoid repeating information already provided"), std::string::npos);
  EXPECT_NE(prompt.find("Break very long responses into logical sections"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesMultiStepProcessGuidance) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify multi-step process guidance
  EXPECT_NE(prompt.find("**Multi-Step Processes:**"), std::string::npos);
  EXPECT_NE(prompt.find("Number steps clearly"), std::string::npos);
  EXPECT_NE(prompt.find("Provide context for why each step is necessary"), std::string::npos);
  EXPECT_NE(prompt.find("Include expected outcomes or verification steps"), std::string::npos);
  EXPECT_NE(prompt.find("Summarize what was accomplished after completion"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesQuestionClarificationGuidance) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify question and clarification guidance
  EXPECT_NE(prompt.find("**Questions and Clarifications:**"), std::string::npos);
  EXPECT_NE(prompt.find("Ask specific, focused questions when information is missing"), std::string::npos);
  EXPECT_NE(prompt.find("Provide context for why you need the information"), std::string::npos);
  EXPECT_NE(prompt.find("Offer reasonable defaults or suggestions"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesChannelAdaptations) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify channel-specific adaptations
  EXPECT_NE(prompt.find("**Channel-Specific Adaptations:**"), std::string::npos);
  EXPECT_NE(prompt.find("**CLI/Terminal**: Use plain text"), std::string::npos);
  EXPECT_NE(prompt.find("**Telegram**: Keep messages concise"), std::string::npos);
  EXPECT_NE(prompt.find("**Web UI**: Use full Markdown"), std::string::npos);
  EXPECT_NE(prompt.find("**Slack/Discord**: Professional tone"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesWhatToAvoid) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify "what to avoid" guidance
  EXPECT_NE(prompt.find("**What to Avoid:**"), std::string::npos);
  EXPECT_NE(prompt.find("Don't repeat yourself unnecessarily"), std::string::npos);
  EXPECT_NE(prompt.find("Don't use overly formal or academic language"), std::string::npos);
  EXPECT_NE(prompt.find("Don't include meta-commentary about your own responses"), std::string::npos);
  EXPECT_NE(prompt.find("Don't apologize excessively"), std::string::npos);
  EXPECT_NE(prompt.find("Don't claim capabilities you don't have"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsIncludesResponseCompleteness) {
  ravbot::PromptComponents components;
  components.include_output_constraints = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify response completeness guidance
  EXPECT_NE(prompt.find("**Response Completeness:**"), std::string::npos);
  EXPECT_NE(prompt.find("Ensure responses are self-contained when possible"), std::string::npos);
  EXPECT_NE(prompt.find("Include all necessary context for understanding"), std::string::npos);
  EXPECT_NE(prompt.find("Provide complete code examples, not fragments"), std::string::npos);
  EXPECT_NE(prompt.find("Reference previous conversation when building on earlier points"), std::string::npos);
}

TEST_F(PromptBuilderTest, OutputConstraintsOmittedWhenDisabled) {
  ravbot::PromptComponents components;
  components.include_output_constraints = false;  // Disabled
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Output constraints should not be included when disabled
  EXPECT_EQ(prompt.find("## Output Constraints"), std::string::npos);
  EXPECT_EQ(prompt.find("### Response Formatting Guidelines"), std::string::npos);
}

// --- BuildWithComponents tests for operation guards (Task 1.1.8) ---

TEST_F(PromptBuilderTest, BuildWithComponentsIncludesOperationGuards) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify operation guards section is included
  EXPECT_NE(prompt.find("## Operation Guards"), std::string::npos);
  EXPECT_NE(prompt.find("### Dangerous Operation Approval Process"), std::string::npos);
  EXPECT_NE(prompt.find("**Operations Requiring Approval:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Approval Process:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Best Practices:**"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesDestructiveFileOperations) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify destructive file operations are documented
  EXPECT_NE(prompt.find("**Destructive File Operations**"), std::string::npos);
  EXPECT_NE(prompt.find("Recursive deletions"), std::string::npos);
  EXPECT_NE(prompt.find("rm -rf"), std::string::npos);
  EXPECT_NE(prompt.find("Overwriting existing files without backup"), std::string::npos);
  EXPECT_NE(prompt.find("Modifying system configuration files"), std::string::npos);
  EXPECT_NE(prompt.find("Explain what will be deleted/modified and ask for confirmation"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesSystemLevelCommands) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify system-level commands are documented
  EXPECT_NE(prompt.find("**System-Level Commands**"), std::string::npos);
  EXPECT_NE(prompt.find("Commands requiring root/administrator privileges"), std::string::npos);
  EXPECT_NE(prompt.find("System service management"), std::string::npos);
  EXPECT_NE(prompt.find("Package installation or system updates"), std::string::npos);
  EXPECT_NE(prompt.find("Network configuration changes"), std::string::npos);
  EXPECT_NE(prompt.find("Firewall or security policy modifications"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesDataModificationAtScale) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify data modification at scale is documented
  EXPECT_NE(prompt.find("**Data Modification at Scale**"), std::string::npos);
  EXPECT_NE(prompt.find("Batch updates to databases"), std::string::npos);
  EXPECT_NE(prompt.find("Mass file renaming or moving operations"), std::string::npos);
  EXPECT_NE(prompt.find("Bulk API calls that modify external resources"), std::string::npos);
  EXPECT_NE(prompt.find("Operations affecting multiple users or accounts"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesExternalNetworkOperations) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify external network operations are documented
  EXPECT_NE(prompt.find("**External Network Operations**"), std::string::npos);
  EXPECT_NE(prompt.find("Sending emails or messages to external recipients"), std::string::npos);
  EXPECT_NE(prompt.find("Making API calls that create, update, or delete external resources"), std::string::npos);
  EXPECT_NE(prompt.find("Uploading data to external services"), std::string::npos);
  EXPECT_NE(prompt.find("Webhook or callback registration"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesCodeExecutionInProduction) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify code execution in production is documented
  EXPECT_NE(prompt.find("**Code Execution in Production**"), std::string::npos);
  EXPECT_NE(prompt.find("Deploying code to production environments"), std::string::npos);
  EXPECT_NE(prompt.find("Running database migrations on production data"), std::string::npos);
  EXPECT_NE(prompt.find("Executing scripts with production credentials"), std::string::npos);
  EXPECT_NE(prompt.find("Modifying production configuration"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesCredentialManagement) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify credential and secret management is documented
  EXPECT_NE(prompt.find("**Credential and Secret Management**"), std::string::npos);
  EXPECT_NE(prompt.find("Reading, writing, or modifying API keys or passwords"), std::string::npos);
  EXPECT_NE(prompt.find("Accessing credential stores or secret management systems"), std::string::npos);
  EXPECT_NE(prompt.find("Sharing or transmitting authentication tokens"), std::string::npos);
  EXPECT_NE(prompt.find("Modifying access control lists or permissions"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesApprovalProcess) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify approval process steps are documented
  EXPECT_NE(prompt.find("**Approval Process:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Assess the Risk**"), std::string::npos);
  EXPECT_NE(prompt.find("**Inform the User**"), std::string::npos);
  EXPECT_NE(prompt.find("**Request Confirmation**"), std::string::npos);
  EXPECT_NE(prompt.find("**Execute Safely**"), std::string::npos);
  EXPECT_NE(prompt.find("**Verify and Report**"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesRiskAssessment) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify risk assessment guidance
  EXPECT_NE(prompt.find("Determine the potential impact"), std::string::npos);
  EXPECT_NE(prompt.find("data loss, system instability, security risk"), std::string::npos);
  EXPECT_NE(prompt.find("Identify what resources will be affected"), std::string::npos);
  EXPECT_NE(prompt.find("Consider reversibility"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesUserInformationGuidance) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify user information guidance
  EXPECT_NE(prompt.find("Clearly describe what the operation will do"), std::string::npos);
  EXPECT_NE(prompt.find("Explain the potential consequences"), std::string::npos);
  EXPECT_NE(prompt.find("Highlight any irreversible changes"), std::string::npos);
  EXPECT_NE(prompt.find("Provide the specific command or action that will be executed"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesConfirmationGuidance) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify confirmation request guidance
  EXPECT_NE(prompt.find("Ask a clear yes/no question"), std::string::npos);
  EXPECT_NE(prompt.find("Wait for explicit user approval before proceeding"), std::string::npos);
  EXPECT_NE(prompt.find("Do not assume consent from vague or ambiguous responses"), std::string::npos);
  EXPECT_NE(prompt.find("If the user seems uncertain, offer to explain further"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesSafeExecutionGuidance) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify safe execution guidance
  EXPECT_NE(prompt.find("Use the safest method available"), std::string::npos);
  EXPECT_NE(prompt.find("move to trash instead of permanent delete"), std::string::npos);
  EXPECT_NE(prompt.find("Apply appropriate sandboxing or resource limits"), std::string::npos);
  EXPECT_NE(prompt.find("Log the operation for audit purposes"), std::string::npos);
  EXPECT_NE(prompt.find("Monitor execution and be prepared to abort"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesVerificationGuidance) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify verification and reporting guidance
  EXPECT_NE(prompt.find("Confirm the operation completed successfully"), std::string::npos);
  EXPECT_NE(prompt.find("Report any errors or unexpected outcomes"), std::string::npos);
  EXPECT_NE(prompt.find("Provide verification steps if appropriate"), std::string::npos);
  EXPECT_NE(prompt.find("Suggest next steps or follow-up actions"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesAutomaticApprovalExceptions) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify automatic approval exceptions are documented
  EXPECT_NE(prompt.find("**Automatic Approval Exceptions:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Trusted Users**: Operations from verified administrators"), std::string::npos);
  EXPECT_NE(prompt.find("**Allowlisted Commands**: Pre-approved safe commands"), std::string::npos);
  EXPECT_NE(prompt.find("**Workspace-Scoped Operations**: File operations within the designated workspace"), std::string::npos);
  EXPECT_NE(prompt.find("**Read-Only Operations**: Operations that only read data"), std::string::npos);
  EXPECT_NE(prompt.find("**Sandboxed Execution**: Operations running in isolated sandbox"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesConfigurationOptions) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify configuration options are documented
  EXPECT_NE(prompt.find("**Configuration:**"), std::string::npos);
  EXPECT_NE(prompt.find("`tools.exec.ask` setting"), std::string::npos);
  EXPECT_NE(prompt.find("`off`: No approval required"), std::string::npos);
  EXPECT_NE(prompt.find("`on-miss`: Require approval only for commands not in allowlist"), std::string::npos);
  EXPECT_NE(prompt.find("`always`: Require approval for all exec operations"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesBestPractices) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify best practices are documented
  EXPECT_NE(prompt.find("**Best Practices:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Default to Safety**: When in doubt, ask for approval"), std::string::npos);
  EXPECT_NE(prompt.find("**Be Transparent**: Always explain what you're about to do"), std::string::npos);
  EXPECT_NE(prompt.find("**Provide Context**: Help the user understand why the operation is necessary"), std::string::npos);
  EXPECT_NE(prompt.find("**Offer Alternatives**: Suggest safer approaches when available"), std::string::npos);
  EXPECT_NE(prompt.find("**Respect Boundaries**: Never try to circumvent approval mechanisms"), std::string::npos);
  EXPECT_NE(prompt.find("**Fail Safely**: If approval is denied or times out, do not proceed"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesExampleApprovalRequest) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify example approval request is included
  EXPECT_NE(prompt.find("**Example Approval Request:**"), std::string::npos);
  EXPECT_NE(prompt.find("⚠️  Dangerous Operation Detected"), std::string::npos);
  EXPECT_NE(prompt.find("Impact:"), std::string::npos);
  EXPECT_NE(prompt.find("This operation cannot be undone"), std::string::npos);
  EXPECT_NE(prompt.find("Do you want to proceed? (yes/no)"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesSecurityNote) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify security note is included
  EXPECT_NE(prompt.find("**Security Note:**"), std::string::npos);
  EXPECT_NE(prompt.find("defense-in-depth measure"), std::string::npos);
  EXPECT_NE(prompt.find("Sandboxing and containerization"), std::string::npos);
  EXPECT_NE(prompt.find("File system permissions and access controls"), std::string::npos);
  EXPECT_NE(prompt.find("Rate limiting and resource quotas"), std::string::npos);
  EXPECT_NE(prompt.find("Audit logging and monitoring"), std::string::npos);
  EXPECT_NE(prompt.find("Input validation and sanitization"), std::string::npos);
  EXPECT_NE(prompt.find("multiple layers of security"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsOmittedWhenDisabled) {
  ravbot::PromptComponents components;
  components.include_operation_guards = false;  // Disabled
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Operation guards should not be included when disabled
  EXPECT_EQ(prompt.find("## Operation Guards"), std::string::npos);
  EXPECT_EQ(prompt.find("### Dangerous Operation Approval Process"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesAllOperationCategories) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify all 6 operation categories are documented
  EXPECT_NE(prompt.find("1. **Destructive File Operations**"), std::string::npos);
  EXPECT_NE(prompt.find("2. **System-Level Commands**"), std::string::npos);
  EXPECT_NE(prompt.find("3. **Data Modification at Scale**"), std::string::npos);
  EXPECT_NE(prompt.find("4. **External Network Operations**"), std::string::npos);
  EXPECT_NE(prompt.find("5. **Code Execution in Production**"), std::string::npos);
  EXPECT_NE(prompt.find("6. **Credential and Secret Management**"), std::string::npos);
}

TEST_F(PromptBuilderTest, OperationGuardsIncludesAllApprovalSteps) {
  ravbot::PromptComponents components;
  components.include_operation_guards = true;
  ravbot::PromptContext context;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify all 5 approval process steps are documented
  EXPECT_NE(prompt.find("1. **Assess the Risk**"), std::string::npos);
  EXPECT_NE(prompt.find("2. **Inform the User**"), std::string::npos);
  EXPECT_NE(prompt.find("3. **Request Confirmation**"), std::string::npos);
  EXPECT_NE(prompt.find("4. **Execute Safely**"), std::string::npos);
  EXPECT_NE(prompt.find("5. **Verify and Report**"), std::string::npos);
}

// --- BuildWithComponents tests for tool summary (Task 1.1.9) ---

TEST_F(PromptBuilderTest, BuildWithComponentsUsesToolSummaryWhenOver100Tools) {
  // Create a mock registry with > 100 tools
  auto large_registry = std::make_shared<ravbot::ToolRegistry>(logger_);
  
  // Add 110 mock tools
  for (int i = 0; i < 110; i++) {
    std::string tool_name = "tool_" + std::to_string(i);
    std::string description = "Description for tool " + std::to_string(i);
    large_registry->RegisterExternalTool(tool_name, description, nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  
  auto large_builder = ravbot::PromptBuilder(
      memory_manager_, skill_loader_, large_registry);
  
  ravbot::PromptComponents components;
  components.use_tool_summary = false;  // Auto-detect based on count
  ravbot::PromptContext context;
  context.tool_count = 110;
  
  auto prompt = large_builder.BuildWithComponents(components, context);
  
  // Verify tool summary mode is used
  EXPECT_NE(prompt.find("## Available Tools (Summary)"), std::string::npos);
  EXPECT_NE(prompt.find("### Tool Summary Mode"), std::string::npos);
  EXPECT_NE(prompt.find("**Total available tools**: 110"), std::string::npos);
}

TEST_F(PromptBuilderTest, ToolSummaryIncludesCategorization) {
  // Create a registry with tools from different categories
  auto categorized_registry = std::make_shared<ravbot::ToolRegistry>(logger_);
  
  // Add tools from different categories (> 100 to trigger summary mode)
  for (int i = 0; i < 30; i++) {
    categorized_registry->RegisterExternalTool("read_file_" + std::to_string(i), 
                                       "Read file " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  for (int i = 0; i < 30; i++) {
    categorized_registry->RegisterExternalTool("exec_command_" + std::to_string(i), 
                                       "Execute command " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  for (int i = 0; i < 30; i++) {
    categorized_registry->RegisterExternalTool("web_search_" + std::to_string(i), 
                                       "Search web " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  for (int i = 0; i < 25; i++) {
    categorized_registry->RegisterExternalTool("memory_recall_" + std::to_string(i), 
                                       "Recall memory " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  
  auto categorized_builder = ravbot::PromptBuilder(
      memory_manager_, skill_loader_, categorized_registry);
  
  ravbot::PromptComponents components;
  ravbot::PromptContext context;
  context.tool_count = 115;
  
  auto prompt = categorized_builder.BuildWithComponents(components, context);
  
  // Verify categories are present
  EXPECT_NE(prompt.find("**File Operations**"), std::string::npos);
  EXPECT_NE(prompt.find("**Command Execution & Process Management**"), std::string::npos);
  EXPECT_NE(prompt.find("**Web & Network**"), std::string::npos);
  EXPECT_NE(prompt.find("**Memory & Context**"), std::string::npos);
}

TEST_F(PromptBuilderTest, ToolSummaryIncludesUsageGuidelines) {
  // Create a registry with > 100 tools
  auto large_registry = std::make_shared<ravbot::ToolRegistry>(logger_);
  for (int i = 0; i < 105; i++) {
    large_registry->RegisterExternalTool("tool_" + std::to_string(i), 
                                 "Tool description " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  
  auto large_builder = ravbot::PromptBuilder(
      memory_manager_, skill_loader_, large_registry);
  
  ravbot::PromptComponents components;
  ravbot::PromptContext context;
  context.tool_count = 105;
  
  auto prompt = large_builder.BuildWithComponents(components, context);
  
  // Verify usage guidelines are included
  EXPECT_NE(prompt.find("### Tool Usage Guidelines"), std::string::npos);
  EXPECT_NE(prompt.find("**How to use tools:**"), std::string::npos);
  EXPECT_NE(prompt.find("**Tool Selection**"), std::string::npos);
  EXPECT_NE(prompt.find("**Parameter Discovery**"), std::string::npos);
  EXPECT_NE(prompt.find("**Error Handling**"), std::string::npos);
  EXPECT_NE(prompt.find("**Chaining**"), std::string::npos);
}

TEST_F(PromptBuilderTest, ToolSummaryIncludesImportantNotes) {
  // Create a registry with > 100 tools
  auto large_registry = std::make_shared<ravbot::ToolRegistry>(logger_);
  for (int i = 0; i < 105; i++) {
    large_registry->RegisterExternalTool("tool_" + std::to_string(i), 
                                 "Tool description " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  
  auto large_builder = ravbot::PromptBuilder(
      memory_manager_, skill_loader_, large_registry);
  
  ravbot::PromptComponents components;
  ravbot::PromptContext context;
  context.tool_count = 105;
  
  auto prompt = large_builder.BuildWithComponents(components, context);
  
  // Verify important notes are included
  EXPECT_NE(prompt.find("**Important Notes:**"), std::string::npos);
  EXPECT_NE(prompt.find("Full tool schemas with parameter details are available"), std::string::npos);
  EXPECT_NE(prompt.find("ensure you provide all required parameters"), std::string::npos);
  EXPECT_NE(prompt.find("Tool descriptions above provide high-level guidance"), std::string::npos);
  EXPECT_NE(prompt.find("Some tools may require specific permissions"), std::string::npos);
}

TEST_F(PromptBuilderTest, ToolSummaryIncludesPerformanceTip) {
  // Create a registry with > 100 tools
  auto large_registry = std::make_shared<ravbot::ToolRegistry>(logger_);
  for (int i = 0; i < 105; i++) {
    large_registry->RegisterExternalTool("tool_" + std::to_string(i), 
                                 "Tool description " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  
  auto large_builder = ravbot::PromptBuilder(
      memory_manager_, skill_loader_, large_registry);
  
  ravbot::PromptComponents components;
  ravbot::PromptContext context;
  context.tool_count = 105;
  
  auto prompt = large_builder.BuildWithComponents(components, context);
  
  // Verify performance tip is included
  EXPECT_NE(prompt.find("**Performance Tip:**"), std::string::npos);
  EXPECT_NE(prompt.find("summary mode is activated when tool count exceeds 100"), std::string::npos);
  EXPECT_NE(prompt.find("reduce prompt size"), std::string::npos);
  EXPECT_NE(prompt.find("improve context window efficiency"), std::string::npos);
}

TEST_F(PromptBuilderTest, ToolSummaryNotUsedWhenToolCountUnder100) {
  // Default registry has < 100 tools
  ravbot::PromptComponents components;
  components.use_tool_summary = false;  // Auto-detect
  ravbot::PromptContext context;
  context.tool_count = 50;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify full tool list is used, not summary
  EXPECT_EQ(prompt.find("## Available Tools (Summary)"), std::string::npos);
  EXPECT_EQ(prompt.find("### Tool Summary Mode"), std::string::npos);
  EXPECT_NE(prompt.find("## Available Tools"), std::string::npos);
}

TEST_F(PromptBuilderTest, ToolSummaryCanBeForcedWithFlag) {
  // Force tool summary even with < 100 tools
  ravbot::PromptComponents components;
  components.use_tool_summary = true;  // Force summary mode
  ravbot::PromptContext context;
  context.tool_count = 50;
  
  auto prompt = builder_->BuildWithComponents(components, context);
  
  // Verify tool summary is used even though count < 100
  EXPECT_NE(prompt.find("## Available Tools (Summary)"), std::string::npos);
  EXPECT_NE(prompt.find("### Tool Summary Mode"), std::string::npos);
}

TEST_F(PromptBuilderTest, ToolSummaryListsToolsWithDescriptions) {
  // Create a registry with > 100 tools
  auto large_registry = std::make_shared<ravbot::ToolRegistry>(logger_);
  large_registry->RegisterExternalTool("read_file", "Read a file from disk", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  large_registry->RegisterExternalTool("write_file", "Write content to a file", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  large_registry->RegisterExternalTool("exec_command", "Execute a shell command", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  
  // Add more tools to exceed 100
  for (int i = 0; i < 100; i++) {
    large_registry->RegisterExternalTool("tool_" + std::to_string(i), 
                                 "Description " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  
  auto large_builder = ravbot::PromptBuilder(
      memory_manager_, skill_loader_, large_registry);
  
  ravbot::PromptComponents components;
  ravbot::PromptContext context;
  context.tool_count = 103;
  
  auto prompt = large_builder.BuildWithComponents(components, context);
  
  // Verify specific tools are listed with descriptions
  EXPECT_NE(prompt.find("`read_file`: Read a file from disk"), std::string::npos);
  EXPECT_NE(prompt.find("`write_file`: Write content to a file"), std::string::npos);
  EXPECT_NE(prompt.find("`exec_command`: Execute a shell command"), std::string::npos);
}

TEST_F(PromptBuilderTest, ToolSummaryGroupsToolsByCategory) {
  // Create a registry with tools from specific categories
  auto categorized_registry = std::make_shared<ravbot::ToolRegistry>(logger_);
  
  // File operations
  categorized_registry->RegisterExternalTool("read_config", "Read configuration", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  categorized_registry->RegisterExternalTool("write_log", "Write log entry", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  categorized_registry->RegisterExternalTool("edit_source", "Edit source file", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  
  // Execution
  categorized_registry->RegisterExternalTool("exec_test", "Execute test", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  categorized_registry->RegisterExternalTool("bash_script", "Run bash script", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  categorized_registry->RegisterExternalTool("process_monitor", "Monitor process", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  
  // Web
  categorized_registry->RegisterExternalTool("web_fetch", "Fetch web content", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  categorized_registry->RegisterExternalTool("search_docs", "Search documentation", nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  
  // Add more tools to exceed 100
  for (int i = 0; i < 95; i++) {
    categorized_registry->RegisterExternalTool("filler_" + std::to_string(i), 
                                       "Filler tool " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  
  auto categorized_builder = ravbot::PromptBuilder(
      memory_manager_, skill_loader_, categorized_registry);
  
  ravbot::PromptComponents components;
  ravbot::PromptContext context;
  context.tool_count = 103;
  
  auto prompt = categorized_builder.BuildWithComponents(components, context);
  
  // Verify tools are grouped by category
  auto file_pos = prompt.find("**File Operations**");
  auto exec_pos = prompt.find("**Command Execution & Process Management**");
  auto web_pos = prompt.find("**Web & Network**");
  
  ASSERT_NE(file_pos, std::string::npos);
  ASSERT_NE(exec_pos, std::string::npos);
  ASSERT_NE(web_pos, std::string::npos);
  
  // Verify tools appear under their respective categories
  auto read_config_pos = prompt.find("`read_config`");
  auto exec_test_pos = prompt.find("`exec_test`");
  auto web_fetch_pos = prompt.find("`web_fetch`");
  
  ASSERT_NE(read_config_pos, std::string::npos);
  ASSERT_NE(exec_test_pos, std::string::npos);
  ASSERT_NE(web_fetch_pos, std::string::npos);
  
  // Verify tools appear after their category headers
  EXPECT_LT(file_pos, read_config_pos);
  EXPECT_LT(exec_pos, exec_test_pos);
  EXPECT_LT(web_pos, web_fetch_pos);
}

TEST_F(PromptBuilderTest, ToolSummaryShowsToolCountPerCategory) {
  // Create a registry with categorized tools
  auto categorized_registry = std::make_shared<ravbot::ToolRegistry>(logger_);
  
  // Add 10 file tools
  for (int i = 0; i < 10; i++) {
    categorized_registry->RegisterExternalTool("read_" + std::to_string(i), 
                                       "Read operation " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  
  // Add 15 exec tools
  for (int i = 0; i < 15; i++) {
    categorized_registry->RegisterExternalTool("exec_" + std::to_string(i), 
                                       "Execute operation " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  
  // Add more to exceed 100
  for (int i = 0; i < 80; i++) {
    categorized_registry->RegisterExternalTool("other_" + std::to_string(i), 
                                       "Other tool " + std::to_string(i), nlohmann::json{}, [](const nlohmann::json&) { return "{}"; });
  }
  
  auto categorized_builder = ravbot::PromptBuilder(
      memory_manager_, skill_loader_, categorized_registry);
  
  ravbot::PromptComponents components;
  ravbot::PromptContext context;
  context.tool_count = 105;
  
  auto prompt = categorized_builder.BuildWithComponents(components, context);
  
  // Verify category counts are shown
  EXPECT_NE(prompt.find("**File Operations** (10 tools)"), std::string::npos);
  EXPECT_NE(prompt.find("**Command Execution & Process Management** (15 tools)"), std::string::npos);
}

// ── P1.4: Silent Reply + Safety Constitution tests ──────────────────────────

// BuildFull should include the [SILENT] token section.
TEST_F(PromptBuilderTest, BuildFullIncludesSilentReplySection) {
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("Silent Reply Protocol"), std::string::npos);
  EXPECT_NE(prompt.find("[SILENT]"), std::string::npos);
}

// BuildFull should include the Safety Constitution section.
TEST_F(PromptBuilderTest, BuildFullIncludesSafetyConstitution) {
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("Safety Constitution"), std::string::npos);
  // Verify core principles are present
  EXPECT_NE(prompt.find("No independent goals"), std::string::npos);
  EXPECT_NE(prompt.find("No self-preservation"), std::string::npos);
  EXPECT_NE(prompt.find("Human oversight first"), std::string::npos);
  EXPECT_NE(prompt.find("Unconditional corrigibility"), std::string::npos);
}

// BuildWithComponents — kFull mode: both sections present
TEST_F(PromptBuilderTest, BuildWithComponents_kFull_HasBothSections) {
  ravbot::PromptComponents components;
  components.mode = ravbot::PromptMode::kFull;
  components.include_silent_reply = true;
  components.include_safety_constitution = true;

  auto prompt = builder_->BuildWithComponents(components);
  EXPECT_NE(prompt.find("Silent Reply Protocol"), std::string::npos);
  EXPECT_NE(prompt.find("Safety Constitution"), std::string::npos);
}

// BuildWithComponents — kMinimal mode: silent reply included, safety omitted
TEST_F(PromptBuilderTest, BuildWithComponents_kMinimal_OmitsSafetyConstitution) {
  ravbot::PromptComponents components;
  components.mode = ravbot::PromptMode::kMinimal;
  components.include_silent_reply = true;
  components.include_safety_constitution = true;  // flag on, but mode overrides

  auto prompt = builder_->BuildWithComponents(components);
  EXPECT_NE(prompt.find("Silent Reply Protocol"), std::string::npos);
  // Safety constitution must be absent in kMinimal
  EXPECT_EQ(prompt.find("Safety Constitution"), std::string::npos);
}

// BuildWithComponents — kNone mode: both sections omitted
TEST_F(PromptBuilderTest, BuildWithComponents_kNone_OmitsBothSections) {
  ravbot::PromptComponents components;
  components.mode = ravbot::PromptMode::kNone;
  components.include_silent_reply = true;
  components.include_safety_constitution = true;

  auto prompt = builder_->BuildWithComponents(components);
  EXPECT_EQ(prompt.find("Silent Reply Protocol"), std::string::npos);
  EXPECT_EQ(prompt.find("Safety Constitution"), std::string::npos);
}

// include_silent_reply = false suppresses the section even in kFull
TEST_F(PromptBuilderTest, BuildWithComponents_SilentReplyFlagFalse) {
  ravbot::PromptComponents components;
  components.mode = ravbot::PromptMode::kFull;
  components.include_silent_reply = false;
  components.include_safety_constitution = true;

  auto prompt = builder_->BuildWithComponents(components);
  EXPECT_EQ(prompt.find("Silent Reply Protocol"), std::string::npos);
  EXPECT_NE(prompt.find("Safety Constitution"), std::string::npos);
}

// include_safety_constitution = false suppresses the section even in kFull
TEST_F(PromptBuilderTest, BuildWithComponents_SafetyConstitutionFlagFalse) {
  ravbot::PromptComponents components;
  components.mode = ravbot::PromptMode::kFull;
  components.include_silent_reply = true;
  components.include_safety_constitution = false;

  auto prompt = builder_->BuildWithComponents(components);
  EXPECT_NE(prompt.find("Silent Reply Protocol"), std::string::npos);
  EXPECT_EQ(prompt.find("Safety Constitution"), std::string::npos);
}
