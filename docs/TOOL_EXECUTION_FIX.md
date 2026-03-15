# QuantClaw 工具执行问题总结

## 问题现象

**用户查询**: "请你搜索github找openclaw的技能的top20列表"

**QuantClaw 响应**: 返回 bash 命令文本，但没有执行
**OpenClaw 响应**: 实际执行工具，返回真实数据

## 技术分析

### 1. 代码层面 ✅

**工具执行逻辑正常**:
```cpp
// src/core/agent_loop.cpp:1799
auto result = tool_registry_->ExecuteTool(tc.name, tc.arguments);
```

**工具注册正常**:
```cpp
// src/tools/tool_registry.cpp
register_tool("exec", ...);
register_tool("bash", ...);
```

**结论**: 代码实现没有问题，工具执行机制是完整的。

### 2. LLM 响应格式

**问题根源**: LLM 没有返回 `tool_calls` 格式，而是直接返回了文本响应。

#### 正确的 LLM 响应（OpenClaw）
```json
{
  "content": "让我搜索 GitHub...",
  "tool_calls": [
    {
      "id": "call_1",
      "name": "exec",
      "arguments": {
        "command": "gh search repos openclaw --sort stars --limit 20"
      }
    }
  ]
}
```

#### 错误的 LLM 响应（QuantClaw）
```json
{
  "content": "让我使用 GitHub CLI 直接搜索...\n\n```bash\ngh search repos openclaw\n```\n\n根据搜索结果..."
}
```

### 3. 可能的原因

#### 原因 A: System Prompt 不够明确 ⭐⭐⭐⭐⭐

QuantClaw 的 system prompt 可能没有明确指示 LLM 使用工具：

```markdown
# 当前可能的 prompt
You have access to these tools:
- exec: Execute shell commands
- bash: Execute bash commands
...

# 应该改进为
When you need to execute commands:
1. Use the `exec` or `bash` tool
2. DO NOT just show the command in a code block
3. Actually call the tool to get results

Example:
User: "Search GitHub for Python projects"
Assistant: [calls exec tool with gh search command]
```

#### 原因 B: 工具描述不够清晰 ⭐⭐⭐⭐

```cpp
// 当前
register_tool("exec", "Execute a shell command and return its output", ...);

// 应该改进为
register_tool("exec",
    "Execute a shell command and return its output. "
    "Use this tool when you need to run commands like gh, curl, etc. "
    "DO NOT just show commands in code blocks - actually execute them.",
    ...);
```

#### 原因 C: 技能说明误导 ⭐⭐⭐

```markdown
# 当前 GitHub 技能
You can interact with GitHub using the `gh` CLI tool via `system.run`.

**Common operations:**
- List repos: `gh repo list`
- Search code: `gh search code "query"`

# 问题: 只是展示命令，没有强调要执行
```

#### 原因 D: 模型行为差异 ⭐⭐

不同的 LLM 模型对工具使用的理解可能不同：
- Claude Opus/Sonnet 可能更倾向于展示命令
- OpenClaw 可能使用了不同的模型或参数

## 解决方案

### 方案 1: 改进 System Prompt ⭐⭐⭐⭐⭐

**优先级**: 最高
**工作量**: 1-2 小时
**效果**: 立竿见影

```cpp
// src/core/prompt_builder.cpp
std::string system_prompt = R"(
You are QuantClaw, an AI assistant with tool execution capabilities.

CRITICAL TOOL USAGE RULES:
1. When you need to execute commands (gh, curl, etc.), you MUST use the exec/bash tool
2. DO NOT just show commands in markdown code blocks
3. DO NOT say "you can run this command" - actually run it yourself
4. After executing tools, present the results to the user

Example of CORRECT behavior:
User: "Search GitHub for Python projects"
You: [calls exec tool] → gets results → presents formatted results

Example of WRONG behavior:
User: "Search GitHub for Python projects"
You: "You can run: ```bash\ngh search repos python\n```"  ❌ WRONG!

Available tools:
- exec/bash: Execute shell commands and get output
- read/write: File operations
- web_search: Search the web
...
)";
```

### 方案 2: 添加工具使用示例 ⭐⭐⭐⭐

在 prompt 中添加具体示例：

```markdown
## Tool Usage Examples

### Example 1: GitHub Search
User: "Find popular Python projects on GitHub"
Assistant: [uses exec tool]
Tool call: exec({"command": "gh search repos python --sort stars --limit 10 --json name,description,stargazersCount"})
Tool result: [JSON data]
Assistant: "I found the top Python projects: 1. python/cpython (50k stars)..."

### Example 2: Web Search
User: "What's the weather in Beijing?"
Assistant: [uses web_search tool]
Tool call: web_search({"query": "Beijing weather"})
Tool result: [search results]
Assistant: "According to the search results, Beijing is currently..."
```

### 方案 3: 改进工具描述 ⭐⭐⭐

```cpp
register_tool("exec",
    "Execute a shell command and return its output. "
    "IMPORTANT: Use this tool to actually run commands like gh, curl, wget, etc. "
    "Do NOT just show commands in code blocks - execute them to get real results.",
    params_schema,
    handler);
```

### 方案 4: 添加 Few-Shot 示例 ⭐⭐⭐⭐

在对话历史中添加示例对话：

```cpp
// Add few-shot examples to conversation history
Message example_user;
example_user.role = "user";
example_user.content.push_back(ContentBlock::MakeText("Search GitHub for openclaw"));

Message example_assistant;
example_assistant.role = "assistant";
example_assistant.content.push_back(ContentBlock::MakeToolUse(
    "example_1", "exec",
    {{"command", "gh search repos openclaw --sort stars --limit 5"}}
));

Message example_result;
example_result.role = "user";
example_result.content.push_back(ContentBlock::MakeToolResult(
    "example_1", "[{\"name\": \"openclaw/openclaw\", \"stars\": 1000}]"
));

// Add these to request.messages before user's actual query
```

### 方案 5: 添加专门的 GitHub 工具 ⭐⭐⭐⭐⭐

```cpp
register_tool("github_search_repos",
    "Search GitHub repositories. Returns structured data.",
    {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"}, {"description", "Search query"}}},
            {"sort", {{"type", "string"}, {"enum", {"stars", "forks", "updated"}}, {"default", "stars"}}},
            {"limit", {{"type", "integer"}, {"default", 20}, {"maximum", 100}}}
        }},
        {"required", {"query"}}
    },
    [this](const nlohmann::json& params) {
        // 实现 GitHub 搜索逻辑
    }
);
```

## 实施计划

### 第一阶段（立即）- 修复 Prompt

1. **修改 `src/core/prompt_builder.cpp`**
   - 添加明确的工具使用指令
   - 强调"执行"而不是"展示"

2. **测试验证**
   ```bash
   # 测试用例
   用户: "搜索 GitHub 上的 Python 项目"
   期望: Agent 调用 exec 工具执行 gh 命令
   ```

### 第二阶段（1-2 天）- 增强工具

1. **添加 GitHub 专用工具**
   - `github_search_repos`
   - `github_search_code`
   - `github_get_repo`

2. **改进工具描述**
   - 所有工具添加使用说明
   - 强调实际执行

### 第三阶段（1 周）- 系统优化

1. **添加 Few-Shot 示例**
2. **改进技能系统**
3. **添加工具使用监控**

## 验证方法

### 测试脚本
```bash
#!/bin/bash
# test_tool_execution.sh

echo "测试 1: GitHub 搜索"
echo "用户: 搜索 GitHub 上 star 最多的 Python 项目" | quantclaw chat

echo "测试 2: 代码搜索"
echo "用户: 在 GitHub 上搜索 async await 的 Python 示例" | quantclaw chat

echo "测试 3: 天气查询"
echo "用户: 北京的天气怎么样" | quantclaw chat
```

### 成功标准
- ✅ Agent 实际调用工具（日志中有 "Executing tool" 记录）
- ✅ 返回真实数据而不是命令文本
- ✅ 与 OpenClaw 行为一致

## 相关文件

- `src/core/prompt_builder.cpp` - System prompt 构建
- `src/core/agent_loop.cpp` - 工具执行逻辑
- `src/tools/tool_registry.cpp` - 工具注册
- `include/quantclaw/builtin_skills.hpp` - 内置技能

## 参考资料

- [Anthropic Tool Use Best Practices](https://docs.anthropic.com/claude/docs/tool-use)
- [OpenAI Function Calling Guide](https://platform.openai.com/docs/guides/function-calling)
- OpenClaw 源代码分析
