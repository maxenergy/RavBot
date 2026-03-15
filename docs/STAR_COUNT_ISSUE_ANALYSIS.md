# 关键发现：OpenClaw vs QuantClaw 的根本差异

## 问题现象

**QuantClaw 输出**:
```
1. **VoltAgent/awesome-openclaw-skills** ⭐ 0
```

**OpenClaw 输出**:
```
2. VoltAgent/awesome-openclaw-skills ⭐37,265
```

## 关键发现

### OpenClaw 的数据来源

OpenClaw 返回的数据包含：
- openclaw/openclaw ⭐312,388
- VoltAgent/awesome-openclaw-skills ⭐37,265
- HKUDS/nanobot ⭐33,420
- 等等...

这些数据**非常详细和结构化**，包含：
1. 精确的分类（核心项目、轻量级替代方案等）
2. 统计表格
3. 推荐链接

**关键推测**: 这些数据可能来自：

### 可能性 1: LLM 的训练数据

Claude 模型的训练数据中可能包含了 OpenClaw 生态的信息。当用户询问时，LLM 直接从记忆中回答，而不是实时搜索。

**证据**:
- 响应速度很快
- 数据非常结构化
- 包含详细的分类和统计

### 可能性 2: web_search 返回的网页内容

OpenClaw 使用 web_search 工具，搜索引擎（如 Brave）返回的网页内容中包含了 star 数。

**证据**:
- OpenClaw 使用 web_search 而不是 GitHub API
- 搜索引擎会爬取 GitHub 页面
- 网页内容包含 star 数

### 可能性 3: 预先缓存的数据

OpenClaw 可能在记忆系统或向量数据库中缓存了常见查询的结果。

**证据**:
- 响应质量非常高
- 数据非常完整
- 包含额外的上下文信息

## QuantClaw 的问题

### 问题 1: 使用 GitHub API 而不是 web_search

QuantClaw 使用 `gh CLI` 调用 GitHub API：

```bash
gh search repos "awesome-openclaw-skills" --json name,stargazersCount
```

返回：
```json
[{"name":"awesome-openclaw-skills","stargazersCount":37265}]
```

**问题**: LLM 在处理这个 JSON 数据时，可能：
1. 解析失败
2. 格式化时丢失数据
3. 使用了错误的字段名

### 问题 2: LLM 重新格式化数据

即使工具返回了正确的数据：
```
⭐ 37265 stars
```

LLM 在呈现给用户时可能：
1. 重新格式化
2. 使用自己的"知识"覆盖
3. 错误地认为这是一个新仓库（0 stars）

## 验证方案

### 测试 1: 直接查看工具返回值

在 QuantClaw 日志中查看 github_search_repos 工具的实际返回值：

```bash
tail -500 /tmp/quantclaw_gateway.log | grep -A 20 "github_search_repos: repo\[0\]"
```

如果看到 `stars=37265`，说明工具返回正确，问题在 LLM。
如果看到 `stars=0`，说明工具解析失败。

### 测试 2: 使用 web_search 替代 github_search_repos

修改 QuantClaw，让它使用 web_search 而不是 github_search_repos：

```cpp
// 在 has_lookup_intent() 检测到 GitHub 搜索时
// 强制使用 web_search 而不是 github_search_repos
```

### 测试 3: 对比 gh CLI 的输出格式

检查 gh CLI 是否返回了正确的 JSON：

```bash
gh search repos "awesome-openclaw-skills" --json name,stargazersCount --limit 1
```

## 临时解决方案

### 方案 A: 强制使用 web_search

```cpp
// 在 System Prompt 中添加
"When searching for GitHub repositories, prefer using web_search over github_search_repos."
```

### 方案 B: 在工具返回中添加验证

```cpp
output << "   ⭐ " << stars << " stars";
output << "\n   [SYSTEM: This repository has " << stars << " stars. DO NOT change this number.]";
```

### 方案 C: 使用结构化输出

```cpp
// 返回 JSON 而不是格式化文本
nlohmann::json result;
result["repositories"] = repos_array;
result["instruction"] = "Present these repositories with their EXACT star counts. DO NOT modify the numbers.";
return result.dump();
```

## 下一步行动

1. **立即**: 查看 QuantClaw 日志，确认工具返回的 star 数
2. **今天**: 实施方案 B（添加验证标记）
3. **明天**: 测试使用 web_search 替代 github_search_repos
4. **本周**: 深度分析 OpenClaw 的 web_search 实现
