// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "ravbot/mcp/mcp_tool_manager.hpp"
#include "ravbot/tools/tool_registry.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

class MCPToolManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("test", null_sink);
    }

    std::shared_ptr<spdlog::logger> logger_;
};

// --- Qualified name construction ---

TEST_F(MCPToolManagerTest, MakeQualifiedName) {
    auto name = ravbot::mcp::MCPToolManager::MakeQualifiedName("code-tools", "lint");
    EXPECT_EQ(name, "mcp__code-tools__lint");
}

TEST_F(MCPToolManagerTest, MakeQualifiedNameSpecialChars) {
    auto name = ravbot::mcp::MCPToolManager::MakeQualifiedName("my-server", "run_tests");
    EXPECT_EQ(name, "mcp__my-server__run_tests");
}

// --- Empty config ---

TEST_F(MCPToolManagerTest, EmptyConfigNoTools) {
    ravbot::mcp::MCPToolManager manager(logger_);
    ravbot::MCPConfig config;  // No servers

    manager.DiscoverTools(config);
    EXPECT_EQ(manager.ToolCount(), 0u);
}

// --- Name resolution ---

TEST_F(MCPToolManagerTest, IsExternalToolFalseForUnknown) {
    ravbot::mcp::MCPToolManager manager(logger_);
    EXPECT_FALSE(manager.IsExternalTool("read"));
    EXPECT_FALSE(manager.IsExternalTool("mcp__unknown__tool"));
}

TEST_F(MCPToolManagerTest, GetServerNameEmptyForUnknown) {
    ravbot::mcp::MCPToolManager manager(logger_);
    EXPECT_EQ(manager.GetServerName("mcp__unknown__tool"), "");
}

TEST_F(MCPToolManagerTest, GetOriginalToolNameEmptyForUnknown) {
    ravbot::mcp::MCPToolManager manager(logger_);
    EXPECT_EQ(manager.GetOriginalToolName("mcp__unknown__tool"), "");
}

// --- Discover tools with invalid server (graceful error) ---

TEST_F(MCPToolManagerTest, DiscoverToolsInvalidServerGraceful) {
    ravbot::mcp::MCPToolManager manager(logger_);
    ravbot::MCPConfig config;
    ravbot::MCPServerConfig server;
    server.name = "bad-server";
    server.url = "http://127.0.0.1:1";  // Should fail to connect
    server.timeout = 1;
    config.servers.push_back(server);

    // Should not throw, just log error
    EXPECT_NO_THROW(manager.DiscoverTools(config));
    EXPECT_EQ(manager.ToolCount(), 0u);
}

// --- Discover tools skips empty name/url ---

TEST_F(MCPToolManagerTest, DiscoverToolsSkipsEmptyNameOrUrl) {
    ravbot::mcp::MCPToolManager manager(logger_);
    ravbot::MCPConfig config;

    ravbot::MCPServerConfig s1;
    s1.name = "";
    s1.url = "http://localhost:9090";
    config.servers.push_back(s1);

    ravbot::MCPServerConfig s2;
    s2.name = "valid";
    s2.url = "";
    config.servers.push_back(s2);

    EXPECT_NO_THROW(manager.DiscoverTools(config));
    EXPECT_EQ(manager.ToolCount(), 0u);
}

// --- Register into ToolRegistry ---

TEST_F(MCPToolManagerTest, RegisterIntoEmptyManager) {
    ravbot::mcp::MCPToolManager manager(logger_);
    ravbot::ToolRegistry registry(logger_);
    registry.RegisterBuiltinTools();

    auto schemas_before = registry.GetToolSchemas();
    manager.RegisterInto(registry);
    auto schemas_after = registry.GetToolSchemas();

    // No MCP tools discovered, so count should not change
    EXPECT_EQ(schemas_before.size(), schemas_after.size());
}

// --- Execute tool with unknown name ---

TEST_F(MCPToolManagerTest, ExecuteToolUnknownThrows) {
    ravbot::mcp::MCPToolManager manager(logger_);
    EXPECT_THROW(manager.ExecuteTool("mcp__unknown__tool", {}), std::runtime_error);
}
