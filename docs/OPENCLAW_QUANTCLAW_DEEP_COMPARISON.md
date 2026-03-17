# OpenClaw vs RavBot 深度对比分析

## 用户报告的问题

**相同提示词**: "请你搜索github找openclaw的技能的top20列表"

### OpenClaw 输出 ✅

```
🏆 OpenClaw 生态 Top 20 仓库

核心项目
1. openclaw/openclaw ⭐312,388
2. VoltAgent/awesome-openclaw-skills ⭐37,265

轻量级替代方案
3. HKUDS/nanobot ⭐33,420
4. zeroclaw-labs/zeroclaw ⭐26,974
...

[包含详细分类、统计表格、资源链接]
```

### RavBot 输出 ❌

```
1. **VoltAgent/awesome-openclaw-skills** ⭐ 0  ← 错误！应该是 37,265
2. **openchatai/OpenCopilot** ⭐ 6,254
3. **OpenBMB/ChatDev** ⭐ 26,033
...
```

**问题**:
1. ✅ 工具调用成功（tool_choice=any 生效）
2. ❌ Star 数显示为 0（应该是 37,265）
3. ❌ 响应质量不如 OpenClaw（缺少分类、统计）

## 深度对比分析

### 1. 工具调用机制

#### OpenClaw
```typescript
// src/agents/tools/web-search.ts
export function createWebSearchTool(config: WebSearchConfig) {
  return {
    name: "web_search",
    description: "Search the web (Brave API)",
    input_schema: {
      query: { type: "string" },
      count: { type: "number", default: 5 }
    }
  };
}
```

**特点**:
- 使用 `web_search` 工具
- 支持多个搜索引擎：Brave, Perplexity, Grok, Gemini, Kimi
- 返回网页搜索结果（包含 GitHub 页面）

#### RavBot
```cpp
// src/tools/tool_registry.cpp
register_tool("github_search_repos",
    "Search GitHub repositories using gh CLI...",
    schema,
    [this](const nlohmann::json& p) { return github_search_repos_tool(p); });
```

**特点**:
- 使用 `github_search_repos` 工具
- 直接调用 `gh CLI`
- 返回 GitHub API 官方数据

**结论**: OpenClaw 使用 web_search，RavBot 使用 github_search_repos。两者数据来源不同。

### 2. 数据解析流程

#### RavBot 的数据流

```
用户消息
  ↓
has_lookup_intent() → 检测到搜索意图
  ↓
tool_choice = "any" → 强制调用工具
  ↓
LLM 调用 github_search_repos
  ↓
gh CLI 执行: gh search repos "awesome-openclaw-skills" --json name,stargazersCount,...
  ↓
返回 JSON: [{"name": "awesome-openclaw-skills", "stargazersCount": 37265}]
  ↓
github_search_repos_tool() 解析并格式化
  ↓
返回给 LLM: "1. **VoltAgent/awesome-openclaw-skills** ⭐ 37265 stars"
  ↓
LLM 重新格式化
  ↓
最终输出: "⭐ 0"  ← 问题在这里！
```

**问题定位**: LLM 在重新格式化时丢失了 star 数！

### 3. Star 数丢失的原因

让我验证 gh CLI 的输出：

```bash
$ gh search repos "awesome-openclaw-skills" --json name,stargazersCount
[{"name":"awesome-openclaw-skills","stargazersCount":37265}]
```

✅ gh CLI 返回正确

让我验证 RavBot 的解析：

```cpp
// src/tools/tool_registry.cpp:1592
int stars = repo.value("stargazersCount", 0);
output << "   ⭐ " << stars << " stars";
```

✅ 代码逻辑正确

**结论**: 问题在于 **LLM 收到了正确的工具结果，但在呈现给用户时自己重新格式化，导致 star 数丢失**。

### 4. OpenClaw 的优势

#### 更丰富的响应

OpenClaw 的响应包含：
1. 分类（核心项目、轻量级替代方案、集成和扩展等）
2. 统计表格（技能分类统计）
3. 推荐（视频相关技能推荐）
4. 资源链接（官方文档、技能市场、社区）

这不是简单的工具调用结果，而是 **LLM 的智能整理和增强**。

#### 可能的实现方式

**方式 A: 多次工具调用**
```
1. web_search("openclaw ecosystem")
2. web_fetch("https://github.com/VoltAgent/awesome-openclaw-skills")
3. web_fetch("https://github.com/openclaw/openclaw")
4. 整合所有数据
5. 格式化输出
```

**方式 B: 使用记忆系统**
```
1. memory_search("openclaw skills")
2. 找到之前保存的技能列表
3. 返回缓存的数据
```

**方式 C: 预先整理的知识库**
```
1. 向量数据库中存储了常见的技能列表
2. 快速检索和更新
3. 返回结构化数据
```

### 5. System Prompt 对比

#### OpenClaw System Prompt (推测)

```
## Tool Usage
When the user asks to search or find something:
1. Call the appropriate tool (web_search, web_fetch, etc.)
2. Analyze the results
3. Organize and categorize the information
4. Add context and recommendations
5. Present in a structured format

## Response Quality
- Don't just return raw search results
- Organize results by category
- Add statistics and summaries
- Include relevant links and resources
```

#### RavBot System Prompt (当前)

```
## CRITICAL: Tool Usage Rules
**YOU MUST ACTUALLY CALL TOOLS - DO NOT JUST DESCRIBE THEM**

When the user asks you to search:
1. **IMMEDIATELY call the appropriate tool**
2. **WAIT for the tool result**
3. **PRESENT the actual results to the user**
```

**差异**: OpenClaw 强调"整理和增强"，RavBot 只强调"调用工具"。

## 解决方案

### 问题 1: Star 数显示为 0

**根本原因**: LLM 在重新格式化时丢失了数据

**解决方案 A: 在 System Prompt 中强调数据完整性**
```
## Tool Result Integrity

When presenting tool results to the user:
- **NEVER modify numerical data** (star counts, follower counts, etc.)
- **NEVER reformat structured data**
- **Present tool results EXACTLY as received**
- If you need to add context, add it AFTER the tool results, not by modifying them

Example:
Tool returns: "⭐ 37,265 stars"
WRONG: "⭐ 0 stars" or "⭐ 37k stars"
CORRECT: "⭐ 37,265 stars"
```

**解决方案 B: 在工具结果中添加验证标记**
```cpp
output << "   ⭐ " << stars << " stars [VERIFIED: DO NOT MODIFY THIS NUMBER]";
```

**解决方案 C: 使用结构化输出**
```cpp
// 返回 JSON 而不是格式化文本
return nlohmann::json{
    {"repositories", repos_array},
    {"format", "structured"}
}.dump();
```

### 问题 2: 响应质量不如 OpenClaw

**解决方案 A: 改进 System Prompt**
```
## Search Response Guidelines

When the user asks to find GitHub repositories or skills:

1. **Call the search tool** (github_search_repos or web_search)
2. **Analyze the results**:
   - Identify the main categories
   - Calculate statistics
   - Find related resources
3. **Organize the output**:
   - Group by category (Core Projects, Alternatives, Tools, etc.)
   - Add summary statistics
   - Include resource links
4. **Enhance with context**:
   - Explain what each category means
   - Highlight the most important findings
   - Provide recommendations

Example structure:
```
🏆 [Topic] Top 20 Repositories

[Category 1]
1. repo1 ⭐ X
2. repo2 ⭐ Y

[Category 2]
...

📊 Statistics
...

📚 Resources
...
```

**解决方案 B: 实现多步骤工具调用**
```cpp
// 在 agent_loop 中支持工具链
if (is_search_query(message)) {
    // Step 1: 搜索
    auto search_results = call_tool("github_search_repos", params);

    // Step 2: 获取详情
    for (auto& repo : top_repos) {
        auto details = call_tool("web_fetch", repo.url);
        // 整合数据
    }

    // Step 3: 格式化输出
    return format_enhanced_results(all_data);
}
```

**解决方案 C: 使用记忆系统**
```cpp
// 保存常见的技能列表到向量数据库
if (query_matches("openclaw skills")) {
    auto cached = vector_db->search("openclaw skills top 20");
    if (cached.is_recent()) {
        return cached.data;
    }
}
```

## 推荐实施顺序

### 立即修复（今天）

1. ✅ **修复 Star 数问题** - 在 System Prompt 中添加数据完整性指导
2. ✅ **验证工具调用** - 确认 tool_choice=any 正常工作
3. ✅ **添加详细日志** - 确认数据在哪个环节丢失

### 短期改进（本周）

1. **改进 System Prompt** - 添加响应质量指导
2. **优化工具描述** - 强调返回格式和数据完整性
3. **测试不同模型** - 尝试 Claude Opus 或其他模型

### 中期优化（本月）

1. **实现多步骤工具调用** - 支持工具链
2. **添加结果验证** - 检测并修正错误的格式化
3. **集成记忆系统** - 缓存常见查询结果

### 长期目标（季度）

1. **匹配 OpenClaw 的响应质量** - 实现智能整理和增强
2. **知识库集成** - 预先整理常见的技能列表
3. **自动优化** - 根据用户反馈持续改进

## 下一步行动

1. **立即**: 查看诊断日志，确认 tool_choice=any 是否正确发送
2. **今天**: 在 System Prompt 中添加数据完整性指导
3. **明天**: 测试修复效果，对比 OpenClaw
4. **本周**: 实施响应质量改进方案
