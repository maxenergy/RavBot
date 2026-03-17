// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <functional>
#include <spdlog/spdlog.h>

namespace ravbot {
    class SessionManager;
    class AgentLoop;
    class PromptBuilder;
    class ToolRegistry;
    class PluginSystem;
    struct RavBotConfig;
}

namespace ravbot::gateway {
    class GatewayServer;
}

namespace ravbot::web {

class WebServer;

void register_api_routes(
    WebServer& server,
    const std::shared_ptr<ravbot::SessionManager>& session_manager,
    const std::shared_ptr<ravbot::AgentLoop>& agent_loop,
    const std::shared_ptr<ravbot::PromptBuilder>& prompt_builder,
    const std::shared_ptr<ravbot::ToolRegistry>& tool_registry,
    const ravbot::RavBotConfig& config,
    ravbot::gateway::GatewayServer& gateway_server,
    const std::shared_ptr<spdlog::logger>& logger,
    const std::function<void()>& reload_fn = nullptr,
    ravbot::PluginSystem* plugin_system = nullptr);

} // namespace ravbot::web
