# 当前故障现象和可能问题分析

**日期**: 2026-03-15 14:12
**状态**: 🔍 调试中

---

## 故障现象

### 用户报告

**Telegram 消息**:
```
在这里面找一些claude code的多代理的skill:VoltAgent/awesome-openclaw-skills
```

**系统响应**:
```
抱歉,处理消息时出错了。请稍后再试。
```

---

## 错误日志

### 核心错误

```
[2026-03-15 14:11:23.246] [error] Anthropic API HTTP 502: {"error":{"type":"api_error","message":"上游 API 调用失败: 非流式 API 请求失败: 400 Bad Request {\"message\":\"Improperly formed request.\",\"reason\":null}"}}
[2026-03-15 14:11:23.246] [error] All models exhausted (primary='anthropic/claude-sonnet-4-5', 0 fallbacks)
[2026-03-15 14:11:24.501] [error] Error processing Telegram message: Failover chain exhausted after 2 attempt(s). No healthy provider available for model 'anthropic/claude-sonnet-4-5'.
```

### 请求信息

```
[2026-03-15 14:11:08.095] [info] Request has 21 tools
[2026-03-15 14:11:08.095] [info] Added 21 tools to request, tool_choice_type=auto
[2026-03-15 14:11:08.095] [info] Request payload size: 25578 bytes, messages: 1
```

```
[2026-03-15 14:11:15.215] [info] Added 21 tools to request, tool_choice_type=auto
[2026-03-15 14:11:15.215] [info] Request payload size: 26090 bytes, messages: 3
```

---

## 已修复的问题

### ✅ 问题 1: Tool Narrowing Bug

**问题**: `narrow_anthropic_tools_for_replay()` 函数缩减工具列表

**修复**: 已禁用该函数

**验证**: 日志显示 "Added 21 tools to request" ✅

**结论**: 此问题已解决

---

## 可能的问题

### 🔍 问题 1: gh CLI 字段名错误

**现象**:
```
[2026-03-15 14:11:15.170] [info] Executing command: gh repo view VoltAgent/awesome-openclaw-skills --json name,description,stargazersCount,...
Unknown JSON field: "stargazersCount"
Available fields:
  ...
  stargazerCount  ← 正确的字段名
  ...
```

**影响**:
- `github_get_repo` 工具执行失败
- 返回 exit_code=1
- 但系统标记为 "Tool execution successful" (可能是误判)

**可能导致的问题**:
- 工具结果为空或错误
- LLM 收到错误的工具结果
- 可能导致后续请求格式错误

**修复方案**:
修改 `src/tools/tool_registry.cpp` 中的字段名:
```cpp
// 修改前
"stargazersCount"

// 修改后
"stargazerCount"
```

---

### 🔍 问题 2: 请求格式仍然不正确

**现象**:
- Anthropic API 返回 "Improperly formed request"
- 即使工具列表已经是 21 个

**可能原因**:

#### 2.1 消息格式问题

**日志**:
```
[2026-03-15 14:11:08.095] [info] Request payload size: 25578 bytes, messages: 1
```

只有 1 条消息,可能是:
- 系统提示词太长
- 消息内容格式不正确
- 缺少必要的消息

#### 2.2 工具定义格式问题

可能的问题:
- 工具的 `input_schema` 格式不正确
- 工具的 `description` 包含特殊字符
- 工具的 `name` 不符合 Anthropic 规范

#### 2.3 系统提示词问题

可能的问题:
- 系统提示词过长 (>25KB)
- 系统提示词包含不兼容的格式
- 系统提示词与工具列表不匹配

#### 2.4 kiro-gateway 代理问题

**配置**: `http://127.0.0.1:8991`

可能的问题:
- 代理修改了请求格式
- 代理添加了额外的验证
- 代理与 Anthropic API 不兼容

---

### 🔍 问题 3: web_search 工具执行

**日志**:
```
[2026-03-15 14:11:18.804] [info] Executing tool: web_search with arguments: {"count":"5","query":"VoltAgent awesome-openclaw-skills multi-agent claude"}
```

**可能问题**:
- web_search 执行后,系统准备第三次请求
- 第三次请求可能触发 replay 逻辑
- 即使 `narrow_anthropic_tools_for_replay()` 被禁用,可能还有其他 replay 逻辑

---

## 调试策略

### 策略 1: 捕获完整的错误 Payload

**已执行**:
- 修改了 payload 保存条件 (20KB-30KB)
- 重新编译并重启 gateway
- 等待用户测试以捕获新的 payload

**下一步**:
- 分析新的 payload 文件
- 检查工具定义格式
- 检查消息格式
- 检查系统提示词

### 策略 2: 修复 gh CLI 字段名

**待执行**:
1. 修改 `src/tools/tool_registry.cpp`
2. 将 `stargazersCount` 改为 `stargazerCount`
3. 重新编译和测试

### 策略 3: 绕过 kiro-gateway 代理

**测试方案**:
1. 临时修改配置,直接连接 Anthropic API
2. 测试是否仍然有 502 错误
3. 确定问题是否在代理层

### 策略 4: 简化请求

**测试方案**:
1. 减少工具数量 (只保留必要的工具)
2. 缩短系统提示词
3. 测试是否能成功

### 策略 5: 对比 OpenClaw

**分析方案**:
1. 查看 OpenClaw 如何处理相同的请求
2. 对比请求格式
3. 找出差异

---

## 时间线

### 14:04 - 第一次修复尝试
- 禁用 `narrow_anthropic_tools_for_replay()`
- 重新编译和重启
- 测试成功 (CLI 测试)

### 14:08 - 用户报告仍然失败
- Telegram 测试失败
- 发现有多个 gateway 进程
- 旧版本仍在运行

### 14:10 - 彻底重启
- 杀掉所有 gateway 进程
- 启动新编译的版本
- 用户测试

### 14:11 - 仍然失败
- 捕获到新的错误日志
- 发现 gh CLI 字段名错误
- 发现 payload 未保存 (大小超限)

### 14:12 - 调整调试策略
- 修改 payload 保存条件
- 重新编译和重启
- 等待新的 payload

---

## 下一步行动

### 立即执行

1. ⏳ **等待用户测试** - 捕获新的 error payload
2. ⏳ **分析 payload** - 找出格式问题
3. ⏳ **修复 gh CLI 字段名** - 解决工具执行问题

### 并行执行

1. **检查 kiro-gateway 日志** - 查看代理层是否有问题
2. **对比 OpenClaw 请求** - 找出差异
3. **简化测试** - 用最小化的请求测试

---

## 可能的根本原因

### 假设 1: 工具定义格式问题 (概率: 60%)

**理由**:
- Anthropic API 明确说 "Improperly formed request"
- 工具列表已经是 21 个 (正确)
- 可能是某个工具的定义格式不正确

**验证方法**:
- 捕获并分析 payload
- 检查每个工具的 `input_schema`

### 假设 2: 系统提示词问题 (概率: 20%)

**理由**:
- 系统提示词很长 (包含 21 个工具的说明)
- 可能包含不兼容的格式或特殊字符

**验证方法**:
- 检查 payload 中的 `system` 字段
- 尝试缩短系统提示词

### 假设 3: kiro-gateway 代理问题 (概率: 15%)

**理由**:
- 使用了代理而不是直连
- 代理可能修改或验证请求

**验证方法**:
- 绕过代理直连 Anthropic API
- 检查代理日志

### 假设 4: 消息格式问题 (概率: 5%)

**理由**:
- 消息数量和格式可能不正确
- Anthropic 对消息格式有严格要求

**验证方法**:
- 检查 payload 中的 `messages` 数组
- 验证消息角色交替

---

**报告生成时间**: 2026-03-15 14:12 UTC
**状态**: 等待用户测试以捕获新的 error payload
**下一步**: 分析 payload 并修复格式问题
