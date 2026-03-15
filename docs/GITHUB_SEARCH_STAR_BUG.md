# GitHub Search Star 数显示错误问题分析

## 问题描述

用户报告 QuantClaw 在 Telegram 中搜索 GitHub 仓库时，显示的 star 数不正确：

**用户看到的结果**:
```
1. **VoltAgent/awesome-openclaw-skills** ⭐ 0
   - A curated list of awesome OpenClaw skills
```

**实际应该是**:
```
1. **VoltAgent/awesome-openclaw-skills** ⭐ 37,262
   - A curated list of awesome OpenClaw skills
```

## 验证测试

### 1. gh CLI 直接测试 ✅

```bash
gh search repos "awesome-openclaw-skills" --sort stars --limit 3 --json name,stargazersCount,owner
```

**结果**: 正确返回 37,262 stars

```json
[{
  "name":"awesome-openclaw-skills",
  "owner":{"login":"VoltAgent"},
  "stargazersCount":37262
}]
```

### 2. QuantClaw 代码检查 ✅

`src/tools/tool_registry.cpp:1592`:
```cpp
output << "   ⭐ " << repo.value("stargazersCount", 0) << " stars";
```

代码逻辑正确，使用 `repo.value("stargazersCount", 0)` 读取 star 数。

## 可能的原因

### 原因 1: JSON 解析失败 ⚠️

如果 gh CLI 返回的 JSON 格式不正确，或者 nlohmann::json 解析失败，会使用默认值 0。

**检查点**:
- exec_tool 是否正确捕获了 gh CLI 的输出？
- JSON 解析是否抛出异常？

### 原因 2: gh CLI 输出被截断 ⚠️

如果 exec_tool 的输出缓冲区太小，可能导致 JSON 不完整。

**检查点**:
- exec_tool 的 timeout 是 30 秒，应该足够
- 需要检查实际的 exec 输出

### 原因 3: LLM 自己格式化了结果 ⚠️

可能 LLM 收到了正确的工具结果，但在呈现给用户时自己重新格式化了，导致 star 数丢失。

**检查点**:
- 查看 agent_loop 日志中的工具返回值
- 查看 LLM 的最终响应

## 诊断步骤

### 步骤 1: 添加详细日志

在 `github_search_repos_tool` 中添加日志：

```cpp
std::string ToolRegistry::github_search_repos_tool(const nlohmann::json& params) {
    // ... 构建命令 ...

    logger_->info("Executing gh command: {}", cmd.str());

    std::string result = exec_tool(exec_params);
    logger_->info("gh CLI raw output length: {}", result.size());
    logger_->info("gh CLI raw output (first 500 chars): {}",
                  result.substr(0, std::min(size_t(500), result.size())));

    try {
        auto repos = nlohmann::json::parse(result);
        logger_->info("Parsed {} repositories", repos.size());

        for (size_t i = 0; i < repos.size(); ++i) {
            const auto& repo = repos[i];
            logger_->info("Repo {}: name={}, stars={}",
                         i,
                         repo.value("name", "unknown"),
                         repo.value("stargazersCount", 0));
        }

        // ... 格式化输出 ...
    }
}
```

### 步骤 2: 检查 agent_loop 日志

查看工具执行的完整日志：

```bash
tail -500 /tmp/quantclaw_gateway.log | grep -A 20 "github_search_repos"
```

### 步骤 3: 对比 OpenClaw

查看 OpenClaw 的实现，看是否有特殊处理。

## 临时解决方案

如果问题是 LLM 重新格式化导致的，可以：

1. **在工具描述中强调**: 修改 `register_tool` 的描述，明确说明返回格式
2. **在 AGENTS.md 中添加指导**: 告诉 LLM 不要修改工具返回的数据
3. **使用更强的 prompt**: 在 system prompt 中强调保持工具结果的完整性

## 下一步

1. 添加详细日志并重新编译
2. 在 Telegram 中重新测试
3. 查看日志确定问题根源
4. 实施修复

## 相关文件

- `src/tools/tool_registry.cpp:1543-1616` - github_search_repos_tool 实现
- `src/core/agent_loop.cpp` - Agent 执行循环
- `AGENTS.md` - Agent 行为指导
