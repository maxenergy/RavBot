# OpenClaw vs RavBot 搜索结果对比分析

## 问题描述

用户在 Telegram 中向两个 bot 发送相同的消息：
```
请你搜索github找openclaw的技能的top20列表
```

### OpenClaw 结果 ✅

根据用户反馈，OpenClaw 返回了正确的结果：
- 找到 VoltAgent/awesome-openclaw-skills
- 显示 37,157+ stars（正确）
- 返回了 20 个技能列表

### RavBot 结果 ❌

用户报告 RavBot 返回：
```
1. **VoltAgent/awesome-openclaw-skills** ⭐ 0
   - A curated list of awesome OpenClaw skills
   - https://github.com/VoltAgent/awesome-openclaw-skills

2. **openchatai/OpenCopilot** ⭐ 6,254
   ...
```

**问题**：
1. ✅ 搜索关键词正确（使用了 "awesome-openclaw-skills"）
2. ❌ Star 数显示为 0（应该是 37,262）
3. ⚠️ 结果质量不如 OpenClaw

## 关键差异分析

### 1. 工具选择

**OpenClaw**:
- 使用 `web_search` 工具
- 支持多个搜索引擎：Brave, Perplexity, Grok, Gemini, Kimi
- 没有专门的 `github_search_repos` 工具

**RavBot**:
- 使用 `github_search_repos` 工具
- 直接调用 `gh CLI`
- 也有 `web_search` 工具作为备选

### 2. 搜索策略

**OpenClaw 可能的策略**:
```
用户: "搜索github找openclaw的技能"
↓
LLM 决定: 使用 web_search
↓
搜索引擎: Brave/Perplexity/等
↓
返回: 网页搜索结果（包含 GitHub 页面）
```

**RavBot 当前策略**:
```
用户: "搜索github找openclaw的技能"
↓
LLM 决定: 使用 github_search_repos
↓
gh CLI: gh search repos "awesome-openclaw-skills"
↓
返回: GitHub API 结果
```

### 3. 数据来源

**OpenClaw (web_search)**:
- 搜索引擎爬取的网页数据
- 可能包含更丰富的上下文信息
- Star 数来自网页内容

**RavBot (github_search_repos)**:
- GitHub API 官方数据
- 结构化的 JSON 数据
- Star 数来自 API 的 `stargazersCount` 字段

## 问题根源

### 可能性 1: gh CLI 返回数据不完整

测试验证：
```bash
gh search repos "awesome-openclaw-skills" --json name,stargazersCount
```

结果：✅ 返回正确的 37,262 stars

**结论**: gh CLI 数据正确，不是数据源问题。

### 可能性 2: JSON 解析错误

RavBot 代码：
```cpp
output << "   ⭐ " << repo.value("stargazersCount", 0) << " stars";
```

如果 `repo["stargazersCount"]` 不存在或类型不匹配，会使用默认值 0。

**需要验证**:
- exec_tool 是否正确捕获了 gh CLI 的输出？
- JSON 解析是否成功？
- stargazersCount 字段是否存在？

### 可能性 3: LLM 重新格式化了结果

可能流程：
```
gh CLI 返回: {"stargazersCount": 37262}
↓
RavBot 格式化: "⭐ 37262 stars"
↓
返回给 LLM
↓
LLM 重新格式化: "⭐ 0 stars" (错误！)
```

**需要验证**:
- 查看 agent_loop 日志中的工具返回值
- 查看 LLM 的最终响应

## 诊断步骤

### 步骤 1: 添加详细日志 ✅

已在 `github_search_repos_tool` 中添加：
```cpp
logger_->info("github_search_repos: executing command: {}", cmd.str());
logger_->info("github_search_repos: raw output length: {} bytes", result.size());
logger_->info("github_search_repos: parsed {} repositories", repos.size());
logger_->info("github_search_repos: repo[{}]: name={}, stars={}", i, name, stars);
```

### 步骤 2: 在 Telegram 中测试

发送消息后查看日志：
```bash
tail -100 /tmp/ravbot_gateway.log | grep github_search_repos
```

### 步骤 3: 对比 OpenClaw 的实现

**需要了解**:
- OpenClaw 使用什么工具？
- 工具参数是什么？
- 返回格式是什么？

## 下一步行动

1. ✅ 已添加详细日志
2. ⏳ 等待用户在 Telegram 中测试
3. ⏳ 分析日志确定问题根源
4. ⏳ 实施修复

## 可能的解决方案

### 方案 A: 修复 JSON 解析

如果是解析问题，确保正确读取 `stargazersCount`：
```cpp
int stars = 0;
if (repo.contains("stargazersCount")) {
    if (repo["stargazersCount"].is_number()) {
        stars = repo["stargazersCount"].get<int>();
    }
}
```

### 方案 B: 使用 web_search 替代

如果 OpenClaw 使用 web_search 效果更好，可以：
1. 让 LLM 优先使用 web_search
2. 或者完全移除 github_search_repos

### 方案 C: 改进工具描述

在工具描述中强调返回格式：
```cpp
"Search GitHub repositories using gh CLI. Returns structured data with accurate star counts."
```

### 方案 D: 防止 LLM 修改数据

在 AGENTS.md 中添加：
```markdown
## Tool Result Integrity

When you receive tool results:
- DO NOT modify numerical data (star counts, follower counts, etc.)
- DO NOT reformat structured data
- Present tool results as-is to the user
```
