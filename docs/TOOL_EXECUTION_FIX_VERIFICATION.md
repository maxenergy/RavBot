# RavBot 工具执行修复验证报告

## 修复时间
2026-03-14

## 问题描述

**原始问题**: RavBot 在收到用户查询时，只返回 bash 命令文本，而不实际执行工具。

**对比**: OpenClaw 会实际执行工具（显示 "There is a tool use"），返回真实数据。

**用户查询示例**: "请你搜索github找openclaw的技能的top20列表"

## 根本原因

LLM 返回文本响应而不是 `tool_calls` 格式，原因：
1. System prompt 没有明确强调"执行"vs"展示"命令
2. 工具描述不够清晰，没有强调实际执行
3. 缺少专门的高级工具（如 GitHub 工具）

## 实施的解决方案

### 1. 改进 System Prompt ✅

**文件**: `src/core/prompt_builder.cpp`

**修改内容**:
```cpp
// Tool usage instructions
prompt << "### CRITICAL: Tool Usage Rules\n"
       << "1. **Execute, Don't Show**: When you need to run commands (gh, curl, etc.), "
       << "you MUST use the exec/bash tool to actually execute them. "
       << "DO NOT just show commands in markdown code blocks.\n"
       << "2. **Get Real Results**: After executing tools, present the actual results to the user. "
       << "Do NOT say \"you can run this command\" - run it yourself and show the output.\n"
       << "3. **Tool Call Format**: Use the proper tool call format with tool name and parameters.\n\n"
       << "**Example of CORRECT behavior:**\n"
       << "User: \"Search GitHub for Python projects\"\n"
       << "You: [calls exec tool with gh command] → gets results → presents formatted results\n\n"
       << "**Example of WRONG behavior:**\n"
       << "User: \"Search GitHub for Python projects\"\n"
       << "You: \"You can run: ```bash\\ngh search repos python\\n```\" ❌ WRONG!\n\n";
```

**效果**: 明确告诉 LLM 必须执行工具而不是展示命令

### 2. 增强工具描述 ✅

**文件**: `src/tools/tool_registry.cpp`

**修改内容**:

#### exec 工具
```cpp
register_tool("exec",
    "Execute a shell command and return its output. "
    "IMPORTANT: Use this tool to actually run commands like gh, curl, wget, git, etc. "
    "DO NOT just show commands in code blocks - execute them to get real results. "
    "Examples: gh search repos, curl https://api.github.com, git status",
    ...);
```

#### bash 工具
```cpp
register_tool("bash",
    "Execute a shell command (alias for exec). "
    "IMPORTANT: Use this to actually run commands, not just show them. "
    "When you need to execute gh, curl, or any CLI tool, use this tool to get real output.",
    ...);
```

**效果**: 工具描述明确强调"执行"而不是"展示"

### 3. 添加专门的 GitHub 工具 ✅

**文件**: `src/tools/tool_registry.cpp`, `include/ravbot/tools/tool_registry.hpp`

**新增工具**:

#### github_search_repos
```cpp
register_tool("github_search_repos",
    "Search GitHub repositories using gh CLI. Returns structured JSON data with repo info. "
    "Requires gh CLI to be installed and authenticated (gh auth login).",
    {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"}, {"description", "Search query"}}},
            {"sort", {{"type", "string"}, {"enum", json::array({"stars", "forks", "updated"})}, {"default", "stars"}}},
            {"limit", {{"type", "integer"}, {"default", 20}, {"minimum", 1}, {"maximum", 100}}}
        }},
        {"required", json::array({"query"})}
    },
    [this](const json& params) { return github_search_repos_tool(params); }
);
```

**实现逻辑**:
- 构造 `gh search repos` 命令
- 使用 `--json` 参数获取结构化数据
- 解析 JSON 并格式化输出
- 包含仓库名、描述、star 数、URL、所有者

#### github_search_code
```cpp
register_tool("github_search_code",
    "Search code on GitHub using gh CLI. Returns code snippets and file locations. "
    "Requires gh CLI to be installed and authenticated.",
    {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"}, {"description", "Code search query"}}},
            {"language", {{"type", "string"}, {"description", "Programming language filter (optional)"}}},
            {"limit", {{"type", "integer"}, {"default", 10}, {"minimum", 1}, {"maximum", 100}}}
        }},
        {"required", json::array({"query"})}
    },
    [this](const json& params) { return github_search_code_tool(params); }
);
```

**实现逻辑**:
- 构造 `gh search code` 命令
- 支持语言过滤（`--language` 参数）
- 返回代码片段、文件路径、仓库信息

#### github_get_repo
```cpp
register_tool("github_get_repo",
    "Get detailed information about a specific GitHub repository using gh CLI. "
    "Requires gh CLI to be installed and authenticated.",
    {
        {"type", "object"},
        {"properties", {
            {"repo", {{"type", "string"}, {"description", "Repository in format owner/repo"}}}
        }},
        {"required", json::array({"repo"})}
    },
    [this](const json& params) { return github_get_repo_tool(params); }
);
```

**实现逻辑**:
- 构造 `gh repo view` 命令
- 使用 `--json` 获取完整仓库信息
- 包含描述、star/fork 数、语言、主题、许可证等

**效果**: 提供高级抽象，LLM 更容易理解和使用

## 代码修改统计

### 修改的文件
1. `src/core/prompt_builder.cpp` - 添加工具使用指令
2. `src/tools/tool_registry.cpp` - 增强工具描述 + 添加 3 个 GitHub 工具
3. `include/ravbot/tools/tool_registry.hpp` - 添加 3 个函数声明

### 新增代码量
- System prompt: ~20 行
- 工具描述增强: ~10 行
- GitHub 工具实现: ~200 行
- 总计: ~230 行

## 编译状态

✅ 编译成功
```
[100%] Built target ravbot
```

## 验证计划

### 测试用例

#### 测试 1: GitHub 仓库搜索
```
用户: 搜索 GitHub 上 star 最多的 5 个 Python 项目
期望: Agent 调用 github_search_repos 工具，返回真实数据
```

#### 测试 2: GitHub 代码搜索
```
用户: 在 GitHub 上搜索 "async await" 的 Python 代码示例
期望: Agent 调用 github_search_code 工具，返回代码片段
```

#### 测试 3: 获取仓库信息
```
用户: 获取 python/cpython 的详细信息
期望: Agent 调用 github_get_repo 工具，返回仓库详情
```

### 成功标准
- ✅ Agent 实际调用工具（日志中有 "Executing tool" 记录）
- ✅ 返回真实数据而不是命令文本
- ✅ 与 OpenClaw 行为一致

## 技术细节

### 工具执行流程

1. **用户查询** → RavBot
2. **Prompt Builder** → 构建包含工具使用规则的 system prompt
3. **LLM 推理** → 理解需要执行工具
4. **返回 tool_calls** → 而不是文本响应
5. **Tool Registry** → 执行对应工具
6. **返回结果** → 格式化后展示给用户

### GitHub 工具实现细节

#### 命令构造
```cpp
std::ostringstream cmd;
cmd << "gh search repos \"" << query << "\" "
    << "--sort " << sort << " "
    << "--limit " << limit << " "
    << "--json name,description,stargazersCount,url,owner";
```

#### JSON 解析
```cpp
json result = json::parse(output);
if (result.is_array()) {
    for (const auto& repo : result) {
        // 提取字段并格式化
    }
}
```

#### 错误处理
```cpp
if (exit_code != 0) {
    return "Error executing gh command: " + output;
}
```

## 与 OpenClaw 的对比

### 修复前
- RavBot: 返回命令文本 ❌
- OpenClaw: 执行工具 ✅

### 修复后（预期）
- RavBot: 执行工具 ✅
- OpenClaw: 执行工具 ✅

## 相关文档

- [工具执行问题分析](TOOL_EXECUTION_ANALYSIS.md)
- [工具执行修复方案](TOOL_EXECUTION_FIX.md)
- [长效记忆验证报告](MEMORY_VERIFICATION_REPORT.md)

## 下一步

1. **运行测试** - 使用真实 LLM 验证工具执行
2. **监控日志** - 确认工具调用记录
3. **对比结果** - 与 OpenClaw 行为对比
4. **性能测试** - 测试工具执行性能
5. **文档更新** - 更新用户文档和 API 文档

## 预期影响

### 用户体验
- ✅ Agent 能够实际执行命令
- ✅ 返回真实数据而不是建议
- ✅ 更接近 OpenClaw 的行为

### 开发体验
- ✅ 更清晰的工具使用模式
- ✅ 更容易添加新的专用工具
- ✅ 更好的错误处理

### 系统性能
- ⚠️ 可能增加 LLM 推理时间（更长的 prompt）
- ✅ 减少用户交互次数（直接执行）
- ✅ 提高任务完成率

## 风险评估

### 低风险
- Prompt 修改不影响现有功能
- 工具描述增强向后兼容
- 新增工具不影响现有工具

### 需要注意
- gh CLI 必须已安装并认证
- 网络连接必须可用
- API 速率限制

## 总结

通过三个层面的改进（Prompt、工具描述、专用工具），RavBot 现在应该能够像 OpenClaw 一样实际执行工具，而不是只返回命令文本。

核心改进：
1. **明确指令** - System prompt 强调"执行"vs"展示"
2. **清晰描述** - 工具描述强调实际执行
3. **高级抽象** - 专用 GitHub 工具更易使用

预期结果：RavBot 工具执行功能完全正常，与 OpenClaw 行为一致。
