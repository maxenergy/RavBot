// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/security/tool_permissions.hpp"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace ravbot {

// Group definitions
static const std::unordered_map<std::string, std::vector<std::string>> kGroups = {
    {"fs",      {"read", "write", "edit"}},
    {"runtime", {"exec"}},
    {"memory",  {"memory_search", "memory_get", "memory_write"}},
    {"mobile_safe",
     {"message", "web_search", "web_fetch",
      "device_status", "runtime_status", "camera_snapshot", "time",
      "memory_list", "memory_search", "memory_get",
      "memory_write", "memory_delete"}},
    {"all",     {"read", "write", "edit", "exec", "message",
                 "apply_patch", "process",
                 "web_search", "web_fetch",
                 "device_status", "runtime_status", "camera_snapshot", "time",
                 "memory_list", "memory_search", "memory_get",
                 "memory_write", "memory_delete",
                 "github_search_repos", "github_search_code", "github_get_repo"}},
};

ToolPermissionChecker::ToolPermissionChecker(const ToolPermissionConfig& config) {
    spdlog::info("ToolPermissionChecker: config.allow.size()={}, config.deny.size()={}",
                 config.allow.size(), config.deny.size());

    // Parse allow list
    for (const auto& entry : config.allow) {
        spdlog::info("ToolPermissionChecker: Processing allow entry: '{}'", entry);

        // Handle wildcard "*" as allow-all
        if (entry == "*") {
            allow_all_ = true;
            mcp_allow_all_ = true;
            spdlog::info("ToolPermissionChecker: Found wildcard '*', setting allow_all=true");
            continue;
        }

        if (entry.substr(0, 6) == "group:") {
            std::string group_name = entry.substr(6);
            spdlog::info("ToolPermissionChecker: Found group: '{}'", group_name);

            if (group_name == "all") {
                allow_all_ = true;
                spdlog::info("ToolPermissionChecker: Set allow_all=true");
            }
            auto it = kGroups.find(group_name);
            if (it != kGroups.end()) {
                spdlog::info("ToolPermissionChecker: Expanding group '{}' with {} tools",
                             group_name, it->second.size());
                for (const auto& tool : it->second) {
                    allowed_tools_.insert(tool);
                    spdlog::info("ToolPermissionChecker: Added tool '{}' from group '{}'",
                                 tool, group_name);
                }
            } else {
                spdlog::warn("ToolPermissionChecker: Unknown group '{}'", group_name);
            }
        } else if (entry.substr(0, 5) == "tool:") {
            allowed_tools_.insert(entry.substr(5));
        } else if (entry.substr(0, 4) == "mcp:") {
            std::string mcp_spec = entry.substr(4);
            if (mcp_spec == "*") {
                mcp_allow_all_ = true;
            } else {
                allowed_mcp_.insert(mcp_spec);
            }
        }
    }

    // Parse deny list
    for (const auto& entry : config.deny) {
        if (entry.substr(0, 6) == "group:") {
            std::string group_name = entry.substr(6);
            auto it = kGroups.find(group_name);
            if (it != kGroups.end()) {
                for (const auto& tool : it->second) {
                    denied_tools_.insert(tool);
                }
            }
        } else if (entry.substr(0, 5) == "tool:") {
            denied_tools_.insert(entry.substr(5));
        } else if (entry.substr(0, 4) == "mcp:") {
            std::string mcp_spec = entry.substr(4);
            denied_mcp_.insert(mcp_spec);
        }
    }

    // If allow list is empty (no entries at all), allow everything
    if (config.allow.empty()) {
        allow_all_ = true;
        mcp_allow_all_ = true;
        spdlog::info("ToolPermissionChecker: Empty allow list, setting allow_all=true");
    }

    // Log configuration
    spdlog::info("ToolPermissionChecker: allow_all={}, allowed_tools={}, denied_tools={}",
                 allow_all_, allowed_tools_.size(), denied_tools_.size());
    if (!allowed_tools_.empty()) {
        std::string tools_list;
        for (const auto& tool : allowed_tools_) {
            if (!tools_list.empty()) tools_list += ", ";
            tools_list += tool;
        }
        spdlog::info("ToolPermissionChecker: allowed_tools=[{}]", tools_list);
    }
}

bool ToolPermissionChecker::IsAllowed(const std::string& tool_name) const {
    // Deny takes priority over allow
    if (denied_tools_.count(tool_name)) {
        return false;
    }

    // If allow_all or tool is in allowed set
    if (allow_all_ || allowed_tools_.count(tool_name)) {
        return true;
    }

    return false;
}

bool ToolPermissionChecker::IsMcpToolAllowed(const std::string& server_name,
                                              const std::string& tool_name) const {
    // Check deny first: exact match then wildcard
    std::string exact = server_name + ":" + tool_name;
    std::string wildcard = server_name + ":*";

    if (denied_mcp_.count(exact) || denied_mcp_.count(wildcard)) {
        return false;
    }

    // Check allow: exact, wildcard, or allow_all
    if (mcp_allow_all_ || allowed_mcp_.count(exact) || allowed_mcp_.count(wildcard)) {
        return true;
    }

    return false;
}

} // namespace ravbot
