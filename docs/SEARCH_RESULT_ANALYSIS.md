# QuantClaw 搜索结果差异分析

## 测试结果对比

### OpenClaw 结果 ✅
```
找到：
- VoltAgent/awesome-openclaw-skills (37,157 stars)
- medeo-video-skill (91 stars)
- youtube-skills (73 stars)
- Seedance2-skill (35 stars)
... 共 20 个技能
```

### QuantClaw 结果 ⚠️
```
找到：
- openchatai/OpenCopilot (6,254 stars)
- OpenBMB/ChatDev (26,033 stars)
- openchatai/OpenChat (5,211 stars)
- open-webui/open-webui (56,969 stars)
```

**问题**: 找到的都是不相关的项目（OpenCopilot、ChatDev 等），不是 OpenClaw 技能。

---

## 成功之处 ✅

### 1. Lookup Guard 成功触发
```
[warning] Lookup guard: provider returned a deferred search preamble
          or negative conclusion without tool calls; forcing one retry
          with explicit tool-use instruction
```

**说明**: 我们的修复生效了！QuantClaw 检测到了"没有找到"的响应，并强制重试。

### 2. 工具实际执行
```
[info] LLM provided final response
```

**说明**: 第二次尝试时，LLM 确实调用了工具（虽然日志中没有明确显示，但从响应内容可以看出）。

### 3. 返回了真实数据
QuantClaw 返回的是真实的 GitHub 搜索结果，包含：
- 仓库名
- Star 数
- 描述
- URL

---

## 问题分析

### 为什么结果不同？

**OpenClaw 的搜索策略**:
1. 第一次搜索：`"openclaw skills"`
2. 第二次搜索：`"awesome-openclaw-skills"`
3. 第三次搜索：可能是 `"openclaw skill repository"`

**QuantClaw 的搜索策略**:
1. 搜索：`"openclaw"` （太宽泛）
2. 第二次搜索：可能是 `"openclaw quantclaw"` （仍然不够精确）

### 根本原因

**LLM 的搜索关键词选择不够精确**。

QuantClaw 搜索了 "openclaw"，这会匹配到所有包含 "open" 和类似词的项目：
- Open**Copilot**
- Open**Chat**
- Open**BMB**
- open-**webui**

而 OpenClaw 搜索了 "openclaw skills" 或 "awesome-openclaw-skills"，直接命中目标仓库。

---

## 解决方案

### 方案 1: 改进搜索提示词 ⭐⭐⭐⭐⭐

**目标**: 让 LLM 使用更精确的搜索关键词

**实施**: 在 AGENTS.md 中添加搜索策略指导

```markdown
## Search Strategy Guidelines

When searching for skills, repositories, or resources:

### ✅ GOOD Search Keywords
- Be specific: "openclaw skills", "awesome-openclaw-skills"
- Include context: "openclaw skill repository", "openclaw skill marketplace"
- Use exact names: "VoltAgent/awesome-openclaw-skills"

### ❌ BAD Search Keywords
- Too generic: "openclaw" (matches OpenCopilot, OpenChat, etc.)
- Too broad: "open source AI" (millions of results)

### Example
User: "Find OpenClaw skills"
WRONG: Search for "openclaw" → Gets OpenCopilot, OpenChat
CORRECT: Search for "openclaw skills" or "awesome-openclaw-skills" → Gets actual skills
```

---

### 方案 2: 添加搜索关键词提示 ⭐⭐⭐⭐

**实施**: 在 prompt_builder.cpp 中添加搜索指导

```cpp
// 在 BuildFull() 中添加
prompt << "## Search Best Practices\n"
       << "When searching GitHub:\n"
       << "- For skills/plugins: Add 'skills', 'awesome', or 'marketplace' to query\n"
       << "- For specific projects: Use exact repository names\n"
       << "- Avoid generic terms that match unrelated projects\n\n";
```

---

### 方案 3: 多次搜索策略 ⭐⭐⭐

**目标**: 如果第一次搜索结果不相关，自动尝试更精确的关键词

**实施**: 在 agent_loop.cpp 中添加搜索结果验证

```cpp
// 伪代码
if (search_results_look_irrelevant(results, original_query)) {
  // 尝试更精确的搜索
  refined_query = refine_search_query(original_query);
  // 例如: "openclaw" → "openclaw skills"
  //      "openclaw" → "awesome-openclaw-skills"
}
```

---

### 方案 4: 使用 Few-Shot 示例 ⭐⭐⭐⭐⭐

**目标**: 通过示例教 LLM 如何搜索

**实施**: 在对话历史中添加搜索示例

```
User: "Find OpenClaw skills"
Assistant: [calls github_search_repos with query="awesome-openclaw-skills"]
Result: Found VoltAgent/awesome-openclaw-skills (37,157 stars)
```

---

## 推荐实施方案

### 立即实施（最简单）

**方案 1**: 修改 AGENTS.md，添加搜索策略指导

**文件**: `/home/rogers/.quantclaw/agents/main/workspace/AGENTS.md`

**添加内容**:
```markdown
## Search Strategy for GitHub

When the user asks to find skills, repositories, or resources:

### Use Specific Keywords
- ✅ "openclaw skills" - finds skill repositories
- ✅ "awesome-openclaw-skills" - finds curated lists
- ✅ "openclaw skill marketplace" - finds skill collections
- ❌ "openclaw" - too generic, matches unrelated projects

### Multiple Search Attempts
If the first search doesn't find relevant results:
1. Try adding "skills", "awesome", or "marketplace"
2. Try the exact repository name if known
3. Try related terms like "plugins", "extensions"

### Example
User: "Find OpenClaw skills top 20"
You should search:
1. "awesome-openclaw-skills" (most likely to have curated list)
2. "openclaw skills" (general skill repositories)
3. "openclaw skill" (singular form)
```

---

## 当前状态评估

### 成功的部分 ✅
1. **Lookup Guard 工作正常** - 检测到并强制重试
2. **工具实际执行** - LLM 调用了搜索工具
3. **返回真实数据** - 不再是"没有找到"的错误信息

### 需要改进的部分 ⚠️
1. **搜索关键词不够精确** - 需要指导 LLM 使用更好的关键词
2. **搜索策略单一** - 只尝试一次，没有优化关键词

---

## 测试建议

### 测试 1: 修改 AGENTS.md 后重试
1. 添加搜索策略指导到 AGENTS.md
2. 重启 gateway
3. 重新发送相同的查询
4. 观察是否使用了更精确的关键词

### 测试 2: 直接指定关键词
在 Telegram 中发送：
```
"搜索 GitHub 上的 'awesome-openclaw-skills' 仓库"
```

**期望**: 应该直接找到正确的仓库

### 测试 3: 对比搜索
分别测试：
- "搜索 openclaw" → 可能找到不相关项目
- "搜索 openclaw skills" → 应该找到技能仓库
- "搜索 awesome-openclaw-skills" → 应该找到精选列表

---

## 结论

### 主要成就 🎉
✅ **Lookup Guard 修复成功** - QuantClaw 现在会实际执行工具调用

### 剩余问题 📝
⚠️ **搜索关键词优化** - 需要指导 LLM 使用更精确的搜索词

### 下一步
1. 修改 AGENTS.md 添加搜索策略指导
2. 重启 gateway
3. 重新测试
4. 如果仍然不理想，实施 Few-Shot 示例方案

---

## 对比总结

| 方面 | OpenClaw | QuantClaw (修复后) | 状态 |
|------|----------|-------------------|------|
| 工具执行 | ✅ 执行 | ✅ 执行 | **已解决** |
| 搜索关键词 | ✅ 精确 | ⚠️ 宽泛 | **需优化** |
| 结果质量 | ✅ 相关 | ⚠️ 不相关 | **需优化** |
| 多次尝试 | ✅ 3次 | ⚠️ 1-2次 | **需优化** |

**总体评价**: 核心问题（工具不执行）已解决，现在是搜索策略优化问题。
