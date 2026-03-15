# QuantClaw 增强 Lookup Guard 修复报告

## 修复时间
2026-03-14 22:31

## 问题分析

### OpenClaw vs QuantClaw 对比

**OpenClaw 行为** ✅:
- 执行了 3 次工具调用
- 返回真实的 GitHub 搜索结果
- 找到了 VoltAgent/awesome-openclaw-skills (37,157 stars)

**QuantClaw 行为** ❌:
- 0 次工具调用
- 返回"没有找到"的错误信息
- 没有实际搜索

### 根本原因

1. **Lookup Guard 检测不完整**
   - 缺少"我来搜索"模式
   - 没有检测否定结论（"没有找到"）

2. **LLM 响应模式**
   - QuantClaw: "我来搜索 GitHub... 搜索结果显示没有找到..."
   - 包含 preamble 但也包含否定结论
   - 原有的 guard 只检测 preamble，不检测否定结论

## 实施的修复

### 修复 1: 增强 Preamble 检测

**文件**: `src/core/agent_loop.cpp` (行 355-362)

**修改前**:
```cpp
static const std::vector<std::string> kCjkMarkers = {
    u8"根据搜索结果", u8"让我为你获取更详细的信息",
    u8"让我帮你查找", u8"让我帮你搜索",
    u8"我来帮你搜索", u8"我来帮你查找",
    u8"让我继续查",   u8"让我获取更详细的信息"};
```

**修改后**:
```cpp
static const std::vector<std::string> kCjkMarkers = {
    u8"根据搜索结果", u8"让我为你获取更详细的信息",
    u8"让我帮你查找", u8"让我帮你搜索",
    u8"我来帮你搜索", u8"我来帮你查找",
    u8"让我继续查",   u8"让我获取更详细的信息",
    u8"我来搜索",     u8"让我搜索",      // ← 新增
    u8"我将搜索",     u8"让我通过"};     // ← 新增
```

**效果**: 现在可以检测到"我来搜索 GitHub..."这种模式

---

### 修复 2: 添加否定结论检测

**文件**: `src/core/agent_loop.cpp` (行 369-387，新增函数)

**新增函数**:
```cpp
static bool response_contains_negative_conclusion(const std::string& text) {
  static const std::vector<std::string> kNegativeMarkers = {
      u8"没有找到", u8"没找到", u8"未找到", u8"不存在",
      u8"没有专门", u8"没有集中", u8"还没有形成", u8"没有公开",
      u8"目前没有", u8"暂时没有",
      "not found", "no results", "doesn't exist", "not available",
      "no public", "currently no"
  };

  for (const auto& marker : kNegativeMarkers) {
    if (text.find(marker) != std::string::npos) {
      return true;
    }
  }
  return false;
}
```

**效果**: 检测到"没有找到"、"没有专门"等否定结论

---

### 修复 3: 增强 Lookup Guard 逻辑

**文件**: `src/core/agent_loop.cpp` (行 1524-1548)

**修改前**:
```cpp
if (!forced_lookup_retry && has_lookup_intent(message) &&
    response_looks_like_unfulfilled_lookup_preamble(response.content)) {
  // 只检测 preamble
}
```

**修改后**:
```cpp
if (!forced_lookup_retry && has_lookup_intent(message) &&
    (response_looks_like_unfulfilled_lookup_preamble(response.content) ||
     response_contains_negative_conclusion(response.content))) {  // ← 新增
  // 检测 preamble 或否定结论
}
```

**效果**: 现在会检测两种情况：
1. 说"我来搜索"但没有实际搜索
2. 说"没有找到"但没有实际搜索

---

### 修复 4: 更强的纠正消息

**修改前**:
```cpp
"Do not stop after saying that you will search. Execute the "
"relevant search or browsing tool now. If no such tool is "
"available, say that clearly instead of implying that results "
"already exist."
```

**修改后**:
```cpp
"STOP. You MUST use the web_search or github_search_repos tool "
"to actually search. DO NOT give conclusions without searching. "
"DO NOT say 'I didn't find' or 'not found' without actually "
"calling the search tool. Call the tool NOW and present the "
"real results."
```

**效果**: 更明确、更强制的语言

---

## 工作原理

### 检测流程

```
用户消息: "请你搜索github找openclaw的技能的top20列表"
    ↓
has_lookup_intent() → ✅ 检测到"搜索"
    ↓
LLM 响应: "我来搜索 GitHub... 没有找到..."
    ↓
response_looks_like_unfulfilled_lookup_preamble() → ✅ 检测到"我来搜索"
    或
response_contains_negative_conclusion() → ✅ 检测到"没有找到"
    ↓
触发 Lookup Guard
    ↓
添加纠正消息: "STOP. You MUST use the tool..."
    ↓
LLM 重试 → 应该调用工具
```

### 预期行为

**第一次响应** (被拦截):
```
"我来搜索 GitHub 上的 OpenClaw 技能仓库：
搜索结果显示没有找到..."
```

**Lookup Guard 触发**:
```
[warn] Lookup guard: provider returned a deferred search preamble
       or negative conclusion without tool calls; forcing one retry
```

**第二次响应** (应该正确):
```
[info] LLM requested 1 tool calls
[info] Executing tool: web_search
[返回真实搜索结果]
```

---

## 测试指南

### 测试用例

**发送到 Telegram**: "请你搜索github找openclaw的技能的top20列表"

### 成功标准

✅ **日志中应该看到**:
```
[warn] Lookup guard: provider returned a deferred search preamble
       or negative conclusion without tool calls; forcing one retry
[info] LLM requested X tool calls
[info] Executing tool: web_search (或 github_search_repos)
```

✅ **Bot 应该返回**:
- 真实的搜索结果
- 仓库列表（包含 star 数、描述、URL）
- 类似 OpenClaw 的响应

❌ **不应该看到**:
- "没有找到"
- "没有专门的列表"
- 没有工具调用的日志

### 监控命令

```bash
# 实时监控日志
tail -f /tmp/quantclaw_gateway.log | grep -E "Lookup guard|tool|Tool|Executing"

# 检查最近的工具调用
grep "LLM requested\|Executing tool" /tmp/quantclaw_gateway.log | tail -10
```

---

## 如果仍然失败

### 诊断步骤

1. **检查日志是否有 Lookup Guard 触发**:
   ```bash
   grep "Lookup guard" /tmp/quantclaw_gateway.log | tail -5
   ```

2. **检查 LLM 是否调用了工具**:
   ```bash
   grep "LLM requested.*tool" /tmp/quantclaw_gateway.log | tail -5
   ```

3. **检查完整的响应**:
   ```bash
   tail -100 /tmp/quantclaw_gateway.log
   ```

### 可能的问题

**问题 1**: Lookup Guard 没有触发
- **原因**: 检测模式仍然不匹配
- **解决**: 添加更多的检测模式

**问题 2**: Lookup Guard 触发但 LLM 仍然不调用工具
- **原因**: 纠正消息不够强
- **解决**: 需要更强的 prompt 或 few-shot 示例

**问题 3**: LLM 调用了工具但返回错误
- **原因**: 工具实现问题
- **解决**: 检查工具执行日志

---

## 下一步计划

### 如果此修复成功 ✅
1. 测试更多场景
2. 验证其他搜索类型（web_search, github_search_code）
3. 记录成功案例

### 如果此修复失败 ❌
1. 实施方案 4: 添加 Few-Shot 示例
2. 实施方案 2: 强化 System Prompt（在最开头）
3. 考虑修改 LLM 模型或参数

---

## 文件修改总结

| 文件 | 修改内容 | 行数 |
|------|---------|------|
| `src/core/agent_loop.cpp` | 增强 preamble 检测 | 355-362 |
| `src/core/agent_loop.cpp` | 新增否定结论检测函数 | 369-387 |
| `src/core/agent_loop.cpp` | 增强 lookup guard 逻辑 | 1524-1548 |

**总修改**: 3 处，新增 ~30 行代码

---

## 编译和部署

✅ **编译**: 成功
✅ **部署**: Gateway 已重启（22:31:01）
✅ **Telegram**: 已连接 (@cppclawbot)

---

## 现在可以测试

请在 Telegram 中发送测试消息，观察 Bot 的行为和日志输出。

**期望**: Bot 应该实际调用工具并返回真实的搜索结果，行为与 OpenClaw 一致。
