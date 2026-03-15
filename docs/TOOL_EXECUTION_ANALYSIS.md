# QuantClaw vs OpenClaw 工具执行对比分析

## 问题描述

**用户查询**: "请你搜索github找openclaw的技能的top20列表"

### QuantClaw 响应
```
让我使用 GitHub CLI 直接搜索 OpenClaw 相关的仓库和代码：

```bash
gh search repos openclaw --sort stars --limit 20 --json name,description,stargazersCount,url,owner
```

根据搜索结果，GitHub 上目前没有找到专门的 OpenClaw 技能 top 20 列表...
```

**问题**: QuantClaw 只返回了 bash 命令的文本，**没有实际执行**这些命令。

### OpenClaw 响应
```
There is a tool use.
There is a tool use.
There is a tool use.
完美！我找到了OpenClaw技能的权威资源。让我为您整理一份视频生成相关的Top技能列表：

🎬 OpenClaw 视频生成技能 Top 20
...
```

**正确**: OpenClaw 实际执行了工具调用，获取了真实数据并返回了详细结果。

## 根本原因分析

### 1. 工具注册差异

#### QuantClaw 工具列表
```cpp
// src/tools/tool_registry.cpp
register_tool("read", ...);
register_tool("write", ...);
register_tool("edit", ...);
register_tool("exec", ...);      // 通用 shell 执行
register_tool("bash", ...);      // exec 的别名
register_tool("web_search", ...);
register_tool("web_fetch", ...);
register_tool("memory_search", ...);
```

**特点**:
- 只有通用的 `exec`/`bash` 工具
- 没有专门的 GitHub 工具
- Agent 需要自己构造 `gh` 命令

#### OpenClaw 工具列表（推测）
```typescript
// 可能有专门的 GitHub 工具
tools.register("github_search_repos", ...);
tools.register("github_search_code", ...);
tools.register("github_get_repo", ...);
```

**特点**:
- 可能有专门的 GitHub API 工具
- 或者有更智能的工具使用机制
- Agent 可以直接调用高级工具

### 2. 技能系统差异

#### QuantClaw 技能
```yaml
# include/quantclaw/builtin_skills.hpp
name: github
description: Interact with GitHub via gh CLI
requires:
  bins:
    - gh
```

**内容**:
```markdown
You can interact with GitHub using the `gh` CLI tool via `system.run`.

**Common operations:**
- List repos: `gh repo list`
- Search code: `gh search code "query" --language python`
```

**问题**: 只是说明文档，没有实际工具实现

#### OpenClaw 技能（推测）
```yaml
name: github
tools:
  - github_search_repos
  - github_search_code
  - github_get_repo
```

**特点**: 可能直接绑定了工具实现

### 3. Agent 行为差异

#### QuantClaw Agent
1. 读取 GitHub 技能说明
2. 理解需要使用 `gh` CLI
3. **生成命令文本** 而不是执行
4. 返回建议给用户

**问题**: Agent 没有实际调用 `exec` 工具执行命令

#### OpenClaw Agent
1. 读取 GitHub 技能说明
2. **实际调用工具** (3 次 tool use)
3. 获取真实数据
4. 处理并返回结果

**正确**: Agent 正确使用了工具执行机制

## 可能的原因

### 原因 1: Prompt 差异
OpenClaw 的 system prompt 可能更明确地指示 Agent 使用工具：

```
When you need to search GitHub:
1. Use the github_search_repos tool
2. Parse the results
3. Present them to the user
```

而 QuantClaw 可能只是：
```
You have access to these tools: exec, bash, read, write...
```

### 原因 2: 工具抽象层次
- **QuantClaw**: 低级工具（exec, bash）
- **OpenClaw**: 高级工具（github_search_repos）

高级工具更容易被 Agent 正确使用。

### 原因 3: 技能加载机制
QuantClaw 的技能可能只是文档，而 OpenClaw 的技能可能包含：
- 工具绑定
- 参数模板
- 执行逻辑

### 原因 4: Agent Loop 实现
QuantClaw 的 Agent Loop 可能在某些情况下跳过了工具执行，直接返回了文本响应。

## 解决方案

### 方案 1: 添加专门的 GitHub 工具 ⭐⭐⭐⭐⭐

```cpp
// src/tools/tool_registry.cpp
register_tool("github_search_repos",
    "Search GitHub repositories",
    {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"}}},
            {"sort", {{"type", "string"}, {"enum", {"stars", "forks", "updated"}}}},
            {"limit", {{"type", "integer"}, {"default", 20}}}
        }},
        {"required", {"query"}}
    },
    [this](const nlohmann::json& params) {
        std::string query = params["query"];
        std::string sort = params.value("sort", "stars");
        int limit = params.value("limit", 20);

        // 构造 gh 命令
        std::ostringstream cmd;
        cmd << "gh search repos \"" << query << "\" "
            << "--sort " << sort << " "
            << "--limit " << limit << " "
            << "--json name,description,stargazersCount,url,owner";

        // 执行命令
        return execute_command(cmd.str());
    }
);
```

**优点**:
- 高级抽象，易于使用
- Agent 更容易理解和调用
- 参数验证和类型检查

### 方案 2: 改进 Prompt ⭐⭐⭐⭐

在 system prompt 中明确指示：

```markdown
When you need to search GitHub:
1. Use the `exec` tool with `gh search repos` command
2. Parse the JSON output
3. Present the results

Example:
exec({"command": "gh search repos openclaw --sort stars --limit 20 --json name,description,stargazersCount,url"})
```

**优点**:
- 无需修改代码
- 快速实现

**缺点**:
- 依赖 Agent 理解能力
- 可能不稳定

### 方案 3: 增强技能系统 ⭐⭐⭐⭐⭐

让技能可以注册工具：

```yaml
# skills/github/SKILL.md
---
name: github
tools:
  - name: github_search_repos
    command: gh search repos {query} --sort {sort} --limit {limit} --json name,description,stargazersCount,url
    params:
      query: string
      sort: string (default: stars)
      limit: integer (default: 20)
---
```

**优点**:
- 声明式配置
- 易于扩展
- 用户可以自定义工具

### 方案 4: 修复 Agent Loop ⭐⭐⭐

检查 Agent Loop 是否正确处理工具调用：

```cpp
// src/core/agent_loop.cpp
if (response.contains("tool_use")) {
    // 执行工具
    auto result = tool_registry_->Execute(tool_name, tool_params);
    // 继续对话
    continue_conversation(result);
} else {
    // 直接返回响应
    return response["content"];
}
```

## 推荐方案

**短期** (1-2 天):
1. 方案 2: 改进 Prompt，明确指示使用 exec 工具
2. 方案 4: 检查并修复 Agent Loop 的工具执行逻辑

**中期** (1 周):
1. 方案 1: 添加常用的高级工具（GitHub, Jira, Slack 等）

**长期** (2-4 周):
1. 方案 3: 增强技能系统，支持声明式工具注册

## 测试验证

### 测试用例 1: GitHub 搜索
```
用户: 请搜索 GitHub 上 star 最多的 Python 项目
期望: Agent 执行 gh search repos 并返回结果
```

### 测试用例 2: 代码搜索
```
用户: 在 GitHub 上搜索 "async await" 的 Python 代码示例
期望: Agent 执行 gh search code 并返回结果
```

### 测试用例 3: 仓库信息
```
用户: 获取 facebook/react 的详细信息
期望: Agent 执行 gh repo view 并返回结果
```

## 相关文件

- `src/tools/tool_registry.cpp` - 工具注册
- `src/core/agent_loop.cpp` - Agent 执行循环
- `include/quantclaw/builtin_skills.hpp` - 内置技能
- `src/core/prompt_builder.cpp` - Prompt 构建

## 参考

- OpenClaw 工具系统文档
- Anthropic Tool Use API 文档
- GitHub CLI 文档
