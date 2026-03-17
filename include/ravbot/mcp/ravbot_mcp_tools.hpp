// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ravbot/mcp/mcp_server.hpp"
#include "ravbot/core/memory_manager.hpp"
#include "ravbot/tools/tool_registry.hpp"
#include <memory>
#include <spdlog/spdlog.h>

namespace ravbot::mcp {

class RavBotMCPTools {
private:
    std::shared_ptr<MemoryManager> memory_manager_;
    std::shared_ptr<ToolRegistry> tool_registry_;
    std::shared_ptr<spdlog::logger> logger_;
    
public:
    RavBotMCPTools(std::shared_ptr<MemoryManager> memory_manager,
                     std::shared_ptr<ToolRegistry> tool_registry,
                     std::shared_ptr<spdlog::logger> logger);
    
    void register_builtin_tools(MCPServer& server);
    
private:
    // Built-in MCP tools
    class ReadFileTool : public MCPTool {
    public:
        ReadFileTool(std::shared_ptr<MemoryManager> memory_manager, 
                    std::shared_ptr<spdlog::logger> logger);
        std::string execute(const nlohmann::json& arguments) override;
    private:
        std::shared_ptr<MemoryManager> memory_manager_;
        std::shared_ptr<spdlog::logger> logger_;
    };
    
    class WriteFileTool : public MCPTool {
    public:
        WriteFileTool(std::shared_ptr<MemoryManager> memory_manager,
                     std::shared_ptr<spdlog::logger> logger);
        std::string execute(const nlohmann::json& arguments) override;
    private:
        std::shared_ptr<MemoryManager> memory_manager_;
        std::shared_ptr<spdlog::logger> logger_;
    };
    
    class EditFileTool : public MCPTool {
    public:
        EditFileTool(std::shared_ptr<MemoryManager> memory_manager,
                    std::shared_ptr<spdlog::logger> logger);
        std::string execute(const nlohmann::json& arguments) override;
    private:
        std::shared_ptr<MemoryManager> memory_manager_;
        std::shared_ptr<spdlog::logger> logger_;
    };
    
    class ExecTool : public MCPTool {
    public:
        ExecTool(std::shared_ptr<ToolRegistry> tool_registry,
                std::shared_ptr<spdlog::logger> logger);
        std::string execute(const nlohmann::json& arguments) override;
    private:
        std::shared_ptr<ToolRegistry> tool_registry_;
        std::shared_ptr<spdlog::logger> logger_;
    };
};

} // namespace ravbot::mcp