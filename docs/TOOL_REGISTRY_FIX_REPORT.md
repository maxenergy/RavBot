# Tool Registry 修复报告

## 问题描述

QuantClaw 在处理 Telegram 消息时,工具注册表返回 0 个工具,导致 LLM 无法调用任何工具(如 github_search_repos)。

## 根本原因

配置文件 `~/.quantclaw/quantclaw.json` 中设置了:
```json
{
  "tools": {
    "allow": ["*"]
  }
}
```

但 `ToolPermissionChecker` 没有处理通配符 `*`,导致:
- `allow_all = false`
- `allowed_tools = []` (空集合)
- 所有工具都被权限检查拒绝

## 修复内容

### 1. 添加通配符支持

**文件**: `src/security/tool_permissions.cpp`

```cpp
// Handle wildcard "*" as allow-all
if (entry == "*") {
    allow_all_ = true;
    mcp_allow_all_ = true;
    spdlog::info("ToolPermissionChecker: Found wildcard '*', setting allow_all=true");
    continue;
}
```

### 2. 扩展 group:all 定义

**文件**: `src/security/tool_permissions.cpp`

```cpp
static const std::unordered_map<std::string, std::vector<std::string>> kGroups = {
    {"fs",      {"read", "write", "edit"}},
    {"runtime", {"exec"}},
    {"all",     {"read", "write", "edit", "exec", "message",
                 "apply_patch", "process",
                 "web_search", "web_fetch",
                 "memory_search", "memory_get",
                 "github_search_repos", "github_search_code", "github_get_repo"}},
};
```

### 3. 添加详细诊断日志

**文件**: `src/security/tool_permissions.cpp` 和 `src/tools/tool_registry.cpp`

- ToolPermissionChecker 构造函数日志
- GetToolSchemas() 过滤日志
- build_request_tools() 日志

## 验证结果

### 修复前
```
[info] ToolPermissionChecker: allow_all=false, allowed_tools=0, denied_tools=0
[info] build_request_tools: Got 0 tool schemas from registry
[info] Request has 0 tools
[warning] No tools provided in request! tool_choice will be ignored.
```

### 修复后
```
[info] ToolPermissionChecker: Found wildcard '*', setting allow_all=true
[info] ToolPermissionChecker: allow_all=true, allowed_tools=0, denied_tools=0
[info] GetToolSchemas: Filtered 21 -> 21 schemas
[info] build_request_tools: Got 21 tool schemas from registry
[info] Request has 21 tools
[info] Added 21 tools to request, tool_choice_type=any
[info] LLM requested 1 tool calls
[info] Executing tool: github_search_repos
```

## 工具调用成功

修复后,LLM 成功调用了工具:

1. **github_search_repos** - 被调用但被 exec approval 拒绝
2. **web_search** - 成功执行并返回结果

## 遗留问题

### 1. Exec Approval 机制

github_search_repos 调用 gh CLI 时被 approval 机制拒绝:
```
[info] Approval requested for command: gh search repos ...
[info] No approval handler or pending, falling back to: denied
```

**建议**: 配置 exec approval 为 "allow" 或 "on-miss" 模式。

### 2. API 502 错误

在第三次工具调用后出现 API 错误:
```
[error] Anthropic API HTTP 502: 上游 API 调用失败: 400 Bad Request
```

这可能是:
- 上游 API 代理问题
- 请求格式问题
- 速率限制

## 总结

核心问题已解决:
- ✅ 工具权限检查修复
- ✅ 工具注册表正常返回 21 个工具
- ✅ LLM 成功调用工具
- ⚠️ Exec approval 需要配置
- ⚠️ API 稳定性需要监控
