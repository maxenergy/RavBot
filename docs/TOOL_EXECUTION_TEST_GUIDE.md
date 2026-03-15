# QuantClaw 工具执行测试指南

## 测试时间
2026-03-14 22:24

## 已应用的修复

### 1. 代码层面修复 ✅
- **文件**: `src/core/prompt_builder.cpp`
- **修改**: 添加了 CRITICAL 工具使用规则（行 102-114）
- **内容**: 明确指示 "Execute, Don't Show"

### 2. 工作区指令修复 ✅
- **文件**: `/home/rogers/.quantclaw/agents/main/workspace/AGENTS.md`
- **修改**: 完全重写，添加详细的工具使用指令
- **内容**:
  - 核心原则：EXECUTE, DON'T SHOW
  - 正确/错误行为对比
  - 具体示例
  - 工具列表

### 3. Gateway 状态 ✅
- **重启时间**: 2026-03-14 22:24:43
- **状态**: 运行中
- **Telegram**: 已连接 (@cppclawbot)

## 测试用例

### 测试 1: 简单搜索
**发送**: "搜索最新的 AI 新闻"

**期望行为**:
- ✅ Bot 调用 `web_search` 工具
- ✅ 返回真实的搜索结果
- ❌ 不应该显示 `web_search({"query": "..."})`

**检查日志**:
```bash
tail -f /tmp/quantclaw_gateway.log | grep -E "tool|Tool|Executing"
```

应该看到：
```
[info] LLM requested 1 tool calls
[info] Executing tool: web_search
```

---

### 测试 2: GitHub 搜索
**发送**: "搜索 GitHub 上 star 最多的 5 个 Python 项目"

**期望行为**:
- ✅ Bot 调用 `github_search_repos` 工具
- ✅ 返回仓库列表（包含 star 数、描述、URL）
- ❌ 不应该显示工具调用代码块

**检查日志**:
```bash
tail -f /tmp/quantclaw_gateway.log | grep -E "github|Tool"
```

---

### 测试 3: 命令执行
**发送**: "检查当前时间"

**期望行为**:
- ✅ Bot 调用 `exec` 工具执行 `date` 命令
- ✅ 返回实际的时间
- ❌ 不应该说 "你可以运行: date"

---

### 测试 4: 代码搜索
**发送**: "在 GitHub 上搜索 'async await' 的 Python 代码"

**期望行为**:
- ✅ Bot 调用 `github_search_code` 工具
- ✅ 返回代码片段和文件位置
- ❌ 不应该显示工具调用

---

## 如果仍然失败

### 问题诊断

**症状**: Bot 仍然显示工具调用而不是执行

**可能原因**:
1. **LLM 模型问题**: 使用的模型可能不支持或不理解工具调用
2. **Prompt 加载问题**: AGENTS.md 没有被正确加载
3. **Provider 问题**: Anthropic provider 解析响应有问题

### 诊断步骤

#### 步骤 1: 验证 AGENTS.md 是否加载
```bash
# 检查日志中是否有 "Agent Behavior" 相关内容
grep -i "agent behavior\|execute.*don't show" /tmp/quantclaw_gateway.log
```

#### 步骤 2: 检查 LLM 响应格式
在 Telegram 中发送测试消息后，立即查看日志：
```bash
tail -100 /tmp/quantclaw_gateway.log | grep -A 10 "LLM response\|tool_calls"
```

如果看到 `"tool_calls": []`，说明 LLM 没有返回工具调用。

#### 步骤 3: 测试不同的模型
编辑 `/home/rogers/.quantclaw/quantclaw.json`，尝试切换模型：
```json
{
  "agent": {
    "model": "anthropic/opus-4.6"  // 或其他模型
  }
}
```

#### 步骤 4: 添加调试日志
临时修改 `src/core/agent_loop.cpp`，在 1467 行后添加：
```cpp
logger_->info("LLM response content: {}", response.content);
logger_->info("LLM tool_calls count: {}", response.tool_calls.size());
```

重新编译并重启。

### 终极解决方案

如果所有方法都失败，问题可能在于：

1. **LLM 模型本身的行为**: 某些模型可能更倾向于"解释"而不是"执行"
2. **需要更强的提示工程**: 可能需要在每个用户消息后添加系统提示

**临时解决方案**: 在 agent_loop.cpp 中添加一个"强制工具使用"的后处理步骤：
- 检测响应中是否包含工具调用的文本描述
- 如果有，解析并实际执行工具
- 将结果替换到响应中

## 监控命令

### 实时日志
```bash
tail -f /tmp/quantclaw_gateway.log
```

### 过滤工具相关日志
```bash
tail -f /tmp/quantclaw_gateway.log | grep -E "tool|Tool|Executing"
```

### 检查进程
```bash
ps aux | grep "quantclaw gateway" | grep -v grep
```

### 检查 Telegram 连接
```bash
curl -s https://api.telegram.org/bot8208097744:AAFZ9qFR5wJjaxQPVt5BI4LDKNQ6ZXTLyyI/getMe | jq
```

## 成功标准

✅ **成功**: 日志中看到 "LLM requested X tool calls" 和 "Executing tool: XXX"
✅ **成功**: Bot 返回真实数据而不是工具调用代码
❌ **失败**: Bot 返回 ` ```tool_name({"param": "value"})``` `
❌ **失败**: 日志中没有 "Executing tool" 记录

## 下一步

1. 在 Telegram 中发送测试消息
2. 观察 Bot 响应
3. 检查日志
4. 如果失败，按照诊断步骤排查
5. 报告结果

## 联系信息

- **Gateway 日志**: `/tmp/quantclaw_gateway.log`
- **配置文件**: `/home/rogers/.quantclaw/quantclaw.json`
- **工作区**: `/home/rogers/.quantclaw/agents/main/workspace/`
- **Bot**: @cppclawbot
