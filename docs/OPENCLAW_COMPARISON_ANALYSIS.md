# RavBot vs OpenClaw 工具执行对比分析

## 测试用例
**用户查询**: "请你搜索github找openclaw的技能的top20列表"

## OpenClaw 行为 ✅ 正确

### 响应过程
```
1. "There is a tool use." (第1次工具调用)
2. "There is a tool use." (第2次工具调用)
3. "There is a tool use." (第3次工具调用)
4. 返回详细的搜索结果（包含20个技能列表）
```

### 执行的工具
- 工具调用次数：**3 次**
- 返回结果：**真实的 GitHub 搜索数据**
- 包含：仓库名、star 数、描述、URL

### 结果质量
- ✅ 找到了 VoltAgent/awesome-openclaw-skills (37,157 stars)
- ✅ 列出了 Top 10 视频生成技能
- ✅ 提供了安装方法
- ✅ 数据真实可靠

---

## RavBot 行为 ❌ 错误

### 响应过程
```
1. 直接返回文本响应
2. 没有任何工具调用
3. 返回"没有找到"的错误信息
```

### 执行的工具
- 工具调用次数：**0 次**
- 返回结果：**编造的回答**
- 内容：声称"没有找到专门的 OpenClaw 技能列表"

### 结果质量
- ❌ 没有实际搜索
- ❌ 返回了错误信息
- ❌ 建议用户手动搜索
- ❌ 完全没有使用可用的工具

---

## 根本原因分析

### 问题定位

**RavBot 的问题**:
1. **LLM 没有返回 `tool_calls` 格式**
2. **直接返回了文本响应**
3. **Lookup guard 没有触发**

**日志证据**:
```
[info] LLM provided final response  ← 直接返回，没有工具调用
```

**缺失的日志**:
```
[info] LLM requested X tool calls    ← 应该有但没有
[info] Executing tool: web_search    ← 应该有但没有
```

### 为什么 Lookup Guard 没有触发？

**Lookup Guard 触发条件**:
```cpp
if (!forced_lookup_retry &&
    has_lookup_intent(message) &&  // ✅ 应该检测到"搜索"
    response_looks_like_unfulfilled_lookup_preamble(response.content)) {  // ❌ 可能这里失败
```

**问题**: `response_looks_like_unfulfilled_lookup_preamble()` 可能没有识别出 RavBot 的响应模式。

让我检查这个函数：

```cpp
static bool response_looks_like_unfulfilled_lookup_preamble(const std::string& text) {
  // 检查是否包含"让我搜索"、"我来搜索"等模式
  // 但 RavBot 的响应是"我来搜索 GitHub 上的 OpenClaw 技能仓库："
  // 然后直接给出了"没找到"的结论
}
```

**RavBot 的响应模式**:
- 开头：✅ "我来搜索 GitHub 上的 OpenClaw 技能仓库："（符合 preamble 模式）
- 中间：❌ "搜索结果显示没有找到..."（直接给出结论，不符合 preamble 模式）

**结论**: Lookup guard 可能检测到了 preamble，但因为响应中包含了"结论"，所以没有触发重试。

---

## OpenClaw vs RavBot 架构差异

### OpenClaw 的优势

1. **更强的 Prompt Engineering**
   - 可能有更明确的"必须使用工具"指令
   - 可能在 system prompt 的更高优先级位置

2. **更好的 LLM 模型配置**
   - 可能使用了不同的模型
   - 可能有不同的温度/top_p 参数
   - 可能有专门的工具使用训练

3. **更完善的 Guard 机制**
   - 可能有更多的检测模式
   - 可能有更强的强制重试逻辑

### RavBot 的问题

1. **Prompt 优先级不够**
   - 工具使用指令可能被其他内容稀释
   - 需要更强的强制性语言

2. **Lookup Guard 不够强**
   - 只检测 "preamble" 模式
   - 没有检测"直接给出错误结论"的模式

3. **缺少强制工具使用机制**
   - 没有"必须使用工具"的硬性要求
   - LLM 可以选择不使用工具

---

## 解决方案

### 方案 1: 增强 Lookup Guard ⭐⭐⭐⭐⭐

**修改**: `src/core/agent_loop.cpp`

```cpp
// 在 1520 行附近添加更强的检测
if (!response.content.empty()) {
  // 检测搜索意图但没有工具调用的情况
  if (!forced_lookup_retry && has_lookup_intent(message)) {
    // 不仅检测 preamble，还检测"没找到"、"没有"等否定结论
    bool looks_like_unfulfilled =
        response_looks_like_unfulfilled_lookup_preamble(response.content) ||
        response_contains_negative_conclusion(response.content);  // 新增

    if (looks_like_unfulfilled) {
      logger_->warn("Lookup guard: forcing tool use for search intent");

      Message assistant_msg;
      assistant_msg.role = "assistant";
      assistant_msg.content.push_back(ContentBlock::MakeText(response.content));
      request.messages.push_back(std::move(assistant_msg));

      Message correction_msg;
      correction_msg.role = "user";
      correction_msg.content.push_back(ContentBlock::MakeText(
          "STOP. You MUST use the web_search or github_search_repos tool to actually search. "
          "DO NOT give conclusions without searching. "
          "Call the tool NOW and present the real results."));
      request.messages.push_back(std::move(correction_msg));

      forced_lookup_retry = true;
      iterations++;
      continue;
    }
  }
}
```

**新增函数**:
```cpp
static bool response_contains_negative_conclusion(const std::string& text) {
  static const std::vector<std::string> kNegativeMarkers = {
      u8"没有找到", u8"没找到", u8"未找到", u8"不存在",
      u8"没有专门", u8"没有集中", u8"还没有形成",
      "not found", "no results", "doesn't exist", "not available"
  };

  for (const auto& marker : kNegativeMarkers) {
    if (text.find(marker) != std::string::npos) {
      return true;
    }
  }
  return false;
}
```

### 方案 2: 在 System Prompt 中添加强制规则 ⭐⭐⭐⭐

**修改**: `src/core/prompt_builder.cpp`

在 BuildFull() 的最开始（第 38 行之前）添加：

```cpp
std::string PromptBuilder::BuildFull(const std::string& /*agent_id*/) const {
  std::ostringstream prompt;

  // ⚠️ CRITICAL: Tool usage enforcement (MUST be first)
  prompt << "# CRITICAL RULE: TOOL USAGE ENFORCEMENT\n\n"
         << "When the user asks you to search, find, lookup, or fetch information:\n"
         << "1. You MUST call the appropriate tool (web_search, github_search_repos, etc.)\n"
         << "2. You MUST NOT give answers without calling tools\n"
         << "3. You MUST NOT say \"I searched\" or \"I found\" without actually calling tools\n"
         << "4. If you don't have information, call a tool to get it\n\n"
         << "**This is a HARD REQUIREMENT. Violating this rule is a critical error.**\n\n";

  // ... 其余的 prompt 构建
```

### 方案 3: 修改 AGENTS.md 使用更强的语言 ⭐⭐⭐

**修改**: `/home/rogers/.ravbot/agents/main/workspace/AGENTS.md`

在文件开头添加：

```markdown
# ⚠️ CRITICAL: MANDATORY TOOL USAGE

## HARD RULE: You MUST Use Tools

When the user asks you to:
- Search (搜索)
- Find (查找)
- Lookup (查询)
- Fetch (获取)
- Check (检查)

You MUST:
1. ✅ Call the appropriate tool
2. ✅ Wait for the tool result
3. ✅ Present the actual result

You MUST NOT:
1. ❌ Give answers without calling tools
2. ❌ Say "I searched" without actually searching
3. ❌ Say "I didn't find" without actually searching
4. ❌ Make up information

**Violating this rule is a CRITICAL ERROR.**
```

### 方案 4: 添加 Few-Shot 示例到对话历史 ⭐⭐⭐⭐⭐

**修改**: `src/core/agent_loop.cpp`

在构建请求时，添加示例对话：

```cpp
// 在 request.messages 的开头添加 few-shot 示例
if (request.messages.empty() || request.messages[0].role != "user") {
  // 示例 1: 搜索查询
  Message example_user1;
  example_user1.role = "user";
  example_user1.content.push_back(ContentBlock::MakeText("搜索 GitHub 上的 Python 项目"));

  Message example_assistant1;
  example_assistant1.role = "assistant";
  example_assistant1.content.push_back(ContentBlock::MakeToolUse(
      "example_1", "github_search_repos",
      {{"query", "python"}, {"sort", "stars"}, {"limit", 5}}
  ));

  Message example_result1;
  example_result1.role = "user";
  example_result1.content.push_back(ContentBlock::MakeToolResult(
      "example_1", "[示例结果]"
  ));

  // 将示例插入到对话开头
  request.messages.insert(request.messages.begin(), {
      example_user1, example_assistant1, example_result1
  });
}
```

---

## 推荐实施顺序

### 第一步: 立即实施（1-2 小时）
1. ✅ 方案 1: 增强 Lookup Guard
2. ✅ 方案 2: 强化 System Prompt

### 第二步: 短期实施（1 天）
3. ✅ 方案 3: 修改 AGENTS.md
4. ✅ 方案 4: 添加 Few-Shot 示例

### 第三步: 验证测试
- 重新测试相同的查询
- 检查日志中是否有 "LLM requested X tool calls"
- 验证返回的是真实数据

---

## 预期效果

实施所有方案后，RavBot 应该：
1. ✅ 检测到搜索意图
2. ✅ 调用 web_search 或 github_search_repos 工具
3. ✅ 返回真实的搜索结果
4. ✅ 行为与 OpenClaw 一致

---

## 总结

**核心问题**: RavBot 的 LLM 没有调用工具，而是直接返回了文本响应。

**根本原因**:
1. Prompt 指令不够强
2. Lookup Guard 检测不够全面
3. 缺少 Few-Shot 示例

**解决方案**:
1. 增强 Lookup Guard（检测否定结论）
2. 强化 System Prompt（使用 CRITICAL/MANDATORY 语言）
3. 添加 Few-Shot 示例（直接展示正确行为）

**优先级**: 方案 1 + 方案 2 是最关键的，应该立即实施。
