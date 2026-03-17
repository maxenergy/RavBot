# GitHub 搜索优化实施计划

## 概述

基于 OpenClaw vs RavBot 深度对比分析，本文档提供具体的实施步骤来优化 RavBot 的 GitHub 搜索功能。

---

## 问题总结

### 已解决 ✅
1. **工具不执行** - Lookup Guard 成功强制 LLM 调用工具
2. **数据完整性** - 提示词明确要求保留原始数据

### 待优化 ⚠️
1. **搜索关键词精确度** - LLM 使用的搜索词过于宽泛
2. **搜索结果验证** - 没有检测结果相关性的机制
3. **多次搜索策略** - 第一次失败后没有自动优化

---

## 实施阶段

### 阶段 1: 诊断和日志增强 (立即实施)

**目标**: 确定问题的确切根源

#### 步骤 1.1: 添加详细日志

**文件**: `src/tools/tool_registry.cpp`

**位置**: `github_search_repos_tool` 函数 (行 1559-1643)

**修改**:
```cpp
std::string ToolRegistry::github_search_repos_tool(const nlohmann::json& params) {
  // ... 现有代码 ...

  // 添加日志: 执行的命令
  logger_->info("github_search_repos: executing command: {}", cmd.str());

  std::string result = exec_tool(exec_params);

  // 添加日志: 原始输出
  logger_->info("github_search_repos: raw output length: {} bytes", result.size());
  logger_->info("github_search_repos: raw output (first 500 chars): {}",
                result.substr(0, std::min(size_t(500), result.size())));

  try {
    auto repos = nlohmann::json::parse(result);
    logger_->info("github_search_repos: parsed {} repositories", repos.size());

    // 添加日志: 每个仓库的详细信息
    for (size_t i = 0; i < repos.size(); ++i) {
      const auto& repo = repos[i];
      std::string name = repo.value("name", "unknown");
      int stars = repo.value("stargazersCount", 0);
      logger_->info("github_search_repos: repo[{}]: name={}, stars={}", i, name, stars);
    }

    // ... 格式化输出 ...
    std::ostringstream output;
    output << "Found " << repos.size() << " repositories:\n\n";

    for (size_t i = 0; i < repos.size(); ++i) {
      const auto& repo = repos[i];
      std::string name = repo.value("name", "unknown");
      int stars = repo.value("stargazersCount", 0);

      output << (i + 1) << ". **" << name << "**";

      if (repo.contains("owner") && repo["owner"].contains("login")) {
        output << " (" << repo["owner"]["login"].get<std::string>() << ")";
      }

      output << "\n   ⭐ " << stars << " stars";

      // ... 其他字段 ...
    }

    std::string formatted_output = output.str();
    logger_->info("github_search_repos: formatted output length: {} bytes",
                  formatted_output.size());
    logger_->info("github_search_repos: formatted output (first 500 chars): {}",
                  formatted_output.substr(0, std::min(size_t(500), formatted_output.size())));

    return formatted_output;
  } catch (const std::exception& e) {
    logger_->error("github_search_repos: JSON parse error: {}", e.what());
    return "Error parsing GitHub search results: " + std::string(e.what());
  }
}
```

#### 步骤 1.2: 添加 Agent Loop 日志

**文件**: `src/core/agent_loop.cpp`

**位置**: 工具结果处理部分

**修改**:
```cpp
// 在处理工具结果时添加日志
for (const auto& tool_result : response.tool_results) {
  logger_->info("Tool result: tool_use_id={}, content_length={}",
                tool_result.tool_use_id, tool_result.content.size());
  logger_->info("Tool result content (first 500 chars): {}",
                tool_result.content.substr(0, std::min(size_t(500), tool_result.content.size())));
}

// 在最终响应前添加日志
logger_->info("Final response content length: {}", response.content.size());
logger_->info("Final response (first 500 chars): {}",
              response.content.substr(0, std::min(size_t(500), response.content.size())));
```

#### 步骤 1.3: 测试和诊断

1. 编译修改后的代码:
```bash
cd /home/rogers/source/develop/RavBot
cmake --build build --target ravbot_gateway
```

2. 重启 gateway:
```bash
pkill -f ravbot_gateway
./build/src/gateway/ravbot_gateway --config config/config.json
```

3. 在 Telegram 中发送测试消息:
```
搜索github找openclaw的技能的top20列表
```

4. 查看日志:
```bash
tail -200 /tmp/ravbot_gateway.log | grep -A 10 "github_search_repos"
```

5. 分析日志确定问题:
   - 检查 LLM 使用的搜索关键词
   - 检查 gh CLI 返回的原始数据
   - 检查格式化后的输出
   - 检查最终响应中的数据

---

### 阶段 2: 搜索关键词优化 (短期实施)

**目标**: 让 LLM 使用更精确的搜索关键词

#### 步骤 2.1: 增强 Prompt 中的搜索策略

**文件**: `src/core/prompt_builder.cpp`

**位置**: `BuildFull()` 函数，行 63 之后

**修改**:
```cpp
std::string PromptBuilder::BuildFull(const std::string& /*agent_id*/) const {
  std::ostringstream prompt;

  // 0. CRITICAL: Tool usage enforcement
  prompt << "## CRITICAL: Tool Usage Rules\n"
         << "**YOU MUST ACTUALLY CALL TOOLS - DO NOT JUST DESCRIBE THEM**\n\n"
         << "When the user asks you to search, fetch, check, or execute something:\n"
         << "1. **IMMEDIATELY call the appropriate tool** (web_search, github_search_repos, exec, etc.)\n"
         << "2. **WAIT for the tool result**\n"
         << "3. **PRESENT the actual results to the user**\n\n"
         << "❌ WRONG: \"Let me search GitHub... I didn't find any results.\"\n"
         << "✅ CORRECT: [calls github_search_repos tool] → receives results → presents them\n\n"
         << "❌ WRONG: \"I'll use the web_search tool to find...\"\n"
         << "✅ CORRECT: [actually calls web_search tool]\n\n"
         << "**NEVER respond with text when a tool call is needed. ALWAYS call the tool first.**\n\n"
         << "## CRITICAL: Data Integrity Rules\n"
         << "**NEVER MODIFY NUMERICAL DATA FROM TOOL RESULTS**\n\n"
         << "When presenting tool results:\n"
         << "- **PRESERVE all numbers EXACTLY as received** (star counts, follower counts, etc.)\n"
         << "- **DO NOT round, abbreviate, or modify numbers**\n"
         << "- **DO NOT change formatting of numerical data**\n\n"
         << "Example:\n"
         << "Tool returns: \"⭐ 37,265 stars\"\n"
         << "❌ WRONG: \"⭐ 0 stars\" or \"⭐ 37k stars\" or \"⭐ 37.3k stars\"\n"
         << "✅ CORRECT: \"⭐ 37,265 stars\" (EXACT copy)\n\n"
         << "If a tool returns star count data, you MUST present it exactly as received.\n"
         << "If you see a number in the tool result, that EXACT number must appear in your response.\n\n"

         // 新增: 搜索关键词策略
         << "## CRITICAL: GitHub Search Keyword Strategy\n"
         << "**ALWAYS use SPECIFIC and PRECISE keywords for GitHub searches**\n\n"
         << "### For Skills/Plugins/Extensions:\n"
         << "- ✅ CORRECT: \"awesome-[project]-skills\", \"[project] skills\", \"[project] skill marketplace\"\n"
         << "- ❌ WRONG: \"[project]\" alone (too generic, matches unrelated projects)\n\n"
         << "### Examples:\n"
         << "User: \"Find OpenClaw skills\"\n"
         << "❌ WRONG: github_search_repos({\"query\": \"openclaw\"})\n"
         << "  → Returns: OpenCopilot, OpenChat, ChatDev (UNRELATED!)\n"
         << "✅ CORRECT: github_search_repos({\"query\": \"awesome-openclaw-skills\"})\n"
         << "  → Returns: VoltAgent/awesome-openclaw-skills (CORRECT!)\n\n"
         << "User: \"Find Python project list\"\n"
         << "❌ WRONG: github_search_repos({\"query\": \"python\"})\n"
         << "  → Returns: Too many unrelated results\n"
         << "✅ CORRECT: github_search_repos({\"query\": \"awesome-python\"})\n"
         << "  → Returns: Curated Python project list (CORRECT!)\n\n"
         << "### Multiple Search Strategy:\n"
         << "If the first search doesn't find relevant results:\n"
         << "1. Add \"skills\", \"awesome\", \"marketplace\", or \"collection\" to the query\n"
         << "2. Try variations: \"skill\" vs \"skills\", \"plugin\" vs \"plugins\"\n"
         << "3. Use exact repository names if known\n\n";

  // ... 其余代码保持不变 ...
}
```

#### 步骤 2.2: 测试关键词优化

1. 重新编译和重启
2. 在 Telegram 中测试:
```
搜索github找openclaw的技能的top20列表
```
3. 检查日志中的搜索关键词:
```bash
tail -100 /tmp/ravbot_gateway.log | grep "github_search_repos: executing command"
```
4. 验证是否使用了 "awesome-openclaw-skills" 而不是 "openclaw"

---

### 阶段 3: 搜索结果验证 (中期实施)

**目标**: 检测搜索结果是否与原始查询相关

#### 步骤 3.1: 实现相关性检测函数

**文件**: `src/core/agent_loop.cpp`

**位置**: 在 `has_lookup_intent()` 等函数附近添加

**新增函数**:
```cpp
// 提取查询中的关键词
static std::vector<std::string> extract_keywords(const std::string& query) {
  std::vector<std::string> keywords;
  std::string lower = to_lower(query);

  // 移除常见停用词
  static const std::set<std::string> stop_words = {
      "the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for",
      "of", "with", "by", "from", "up", "about", "into", "through", "during",
      u8"的", u8"了", u8"在", u8"是", u8"我", u8"有", u8"和", u8"就",
      u8"不", u8"人", u8"都", u8"一", u8"一个", u8"上", u8"也", u8"很",
      u8"到", u8"说", u8"要", u8"去", u8"你", u8"会", u8"着", u8"没有",
      u8"看", u8"好", u8"自己", u8"这", u8"那", u8"里", u8"就是"
  };

  // 简单的分词 (按空格和标点分割)
  std::istringstream iss(lower);
  std::string word;
  while (iss >> word) {
    // 移除标点
    word.erase(std::remove_if(word.begin(), word.end(),
                              [](char c) { return std::ispunct(c); }),
               word.end());

    // 如果不是停用词且长度 >= 3，添加到关键词列表
    if (word.length() >= 3 && stop_words.find(word) == stop_words.end()) {
      keywords.push_back(word);
    }
  }

  return keywords;
}

// 检测搜索结果是否与查询相关
static bool search_results_look_irrelevant(const std::string& response,
                                           const std::string& original_query) {
  // 提取查询中的关键词
  std::vector<std::string> query_keywords = extract_keywords(original_query);

  if (query_keywords.empty()) {
    return false;  // 无法判断，假设相关
  }

  // 检查响应中是否包含这些关键词
  std::string lower_response = to_lower(response);
  int match_count = 0;

  for (const auto& keyword : query_keywords) {
    if (lower_response.find(keyword) != std::string::npos) {
      match_count++;
    }
  }

  // 如果匹配度低于 50%，认为结果不相关
  double match_ratio = static_cast<double>(match_count) / query_keywords.size();
  return match_ratio < 0.5;
}
```

#### 步骤 3.2: 集成相关性检测到 Agent Loop

**文件**: `src/core/agent_loop.cpp`

**位置**: 在 Lookup Guard 之后添加

**修改**:
```cpp
// 在 agent_loop.cpp 的主循环中，Lookup Guard 之后添加

// 搜索结果相关性验证
if (!forced_refined_search_retry &&
    has_lookup_intent(message) &&
    !response.tool_calls.empty() &&  // 确保已经调用了工具
    search_results_look_irrelevant(response.content, message)) {

  logger_->warn("Search refinement guard: search results appear irrelevant to query; "
                "forcing retry with more specific keywords");

  Message correction_msg;
  correction_msg.role = "user";
  correction_msg.content.push_back(ContentBlock::MakeText(
      "STOP. The search results don't match the user's request. "
      "You MUST try again with MORE SPECIFIC keywords:\n\n"
      "Guidelines:\n"
      "1. Add 'skills', 'awesome', 'marketplace', or 'collection' to the query\n"
      "2. Use exact repository names if known\n"
      "3. Avoid generic terms that match unrelated projects\n\n"
      "Example:\n"
      "User: \"Find OpenClaw skills\"\n"
      "WRONG: github_search_repos({\"query\": \"openclaw\"}) → OpenCopilot, OpenChat (WRONG!)\n"
      "CORRECT: github_search_repos({\"query\": \"awesome-openclaw-skills\"}) → Skill list (CORRECT!)\n\n"
      "Now call github_search_repos again with BETTER keywords."));

  request.messages.push_back(std::move(correction_msg));

  forced_refined_search_retry = true;
  iterations++;
  continue;
}
```

#### 步骤 3.3: 测试相关性验证

1. 重新编译和重启
2. 在 Telegram 中测试宽泛的查询:
```
搜索 openclaw
```
3. 观察是否触发相关性验证并自动重试
4. 检查日志:
```bash
tail -100 /tmp/ravbot_gateway.log | grep "Search refinement guard"
```

---

### 阶段 4: Few-Shot 示例注入 (长期优化)

**目标**: 通过示例教 LLM 如何正确搜索

#### 步骤 4.1: 实现示例注入

**文件**: `src/core/agent_loop.cpp`

**位置**: 在构建请求时添加

**修改**:
```cpp
// 在 agent_loop.cpp 的 Run() 函数中，构建请求时添加

// 如果是搜索意图且是第一条消息，注入 Few-Shot 示例
if (has_lookup_intent(message) && request.messages.size() == 1) {
  logger_->info("Injecting Few-Shot search examples");

  // 示例 1: 用户消息
  Message example_user;
  example_user.role = "user";
  example_user.content.push_back(ContentBlock::MakeText(
      "Find OpenClaw skills"));

  // 示例 2: Assistant 调用工具
  Message example_assistant;
  example_assistant.role = "assistant";
  example_assistant.content.push_back(ContentBlock::MakeToolUse(
      "example_tool_use_1",
      "github_search_repos",
      nlohmann::json{{"query", "awesome-openclaw-skills"}, {"limit", 20}}));

  // 示例 3: 工具结果
  Message example_result;
  example_result.role = "user";
  example_result.content.push_back(ContentBlock::MakeToolResult(
      "example_tool_use_1",
      "Found 1 repository:\n\n"
      "1. **awesome-openclaw-skills** (VoltAgent)\n"
      "   ⭐ 37262 stars | 📝 Markdown\n"
      "   A curated list of awesome OpenClaw skills\n"
      "   🔗 https://github.com/VoltAgent/awesome-openclaw-skills"));

  // 示例 4: Assistant 最终响应
  Message example_final;
  example_final.role = "assistant";
  example_final.content.push_back(ContentBlock::MakeText(
      "I found the OpenClaw skills repository:\n\n"
      "**VoltAgent/awesome-openclaw-skills** ⭐ 37,262 stars\n"
      "A curated list of awesome OpenClaw skills\n"
      "https://github.com/VoltAgent/awesome-openclaw-skills"));

  // 插入到实际消息之前
  auto original_messages = request.messages;
  request.messages.clear();
  request.messages.push_back(example_user);
  request.messages.push_back(example_assistant);
  request.messages.push_back(example_result);
  request.messages.push_back(example_final);
  request.messages.insert(request.messages.end(),
                         original_messages.begin(),
                         original_messages.end());
}
```

#### 步骤 4.2: 测试 Few-Shot 示例

1. 重新编译和重启
2. 在 Telegram 中测试:
```
找 Python 项目列表
```
3. 观察 LLM 是否学习了示例中的搜索策略
4. 检查是否使用了 "awesome-python" 而不是 "python"

---

## 测试矩阵

### 测试用例

| 测试 ID | 输入 | 期望搜索关键词 | 期望结果 |
|---------|------|---------------|---------|
| T1 | "搜索 openclaw 技能" | "awesome-openclaw-skills" 或 "openclaw skills" | VoltAgent/awesome-openclaw-skills |
| T2 | "找 Python 项目列表" | "awesome-python" | awesome-python 仓库 |
| T3 | "搜索 awesome-openclaw-skills" | "awesome-openclaw-skills" | VoltAgent/awesome-openclaw-skills |
| T4 | "找 React 组件库" | "awesome-react" 或 "react components" | React 组件库列表 |
| T5 | "搜索 openclaw" (宽泛) | 第一次: "openclaw"<br>第二次: "awesome-openclaw-skills" | 触发相关性验证并重试 |

### 验证点

| 验证点 | 检查方法 | 期望结果 |
|--------|---------|---------|
| 工具执行 | 日志中有 "github_search_repos: executing command" | ✅ 工具被调用 |
| 搜索关键词 | 日志中的命令包含精确关键词 | ✅ 使用 "awesome-*" 或 "* skills" |
| 原始数据 | 日志中的 "raw output" 包含正确的 stargazersCount | ✅ 数据正确 |
| 格式化输出 | 日志中的 "formatted output" 包含正确的星数 | ✅ 格式化正确 |
| 最终响应 | 日志中的 "Final response" 包含正确的星数 | ✅ 数据未被修改 |
| 相关性验证 | 宽泛查询触发 "Search refinement guard" | ✅ 自动重试 |

---

## 回滚计划

如果优化导致问题，可以按以下步骤回滚：

### 回滚阶段 1 (日志)
```bash
git checkout HEAD -- src/tools/tool_registry.cpp
git checkout HEAD -- src/core/agent_loop.cpp
```

### 回滚阶段 2 (Prompt)
```bash
git checkout HEAD -- src/core/prompt_builder.cpp
```

### 回滚阶段 3 (相关性验证)
```bash
git checkout HEAD -- src/core/agent_loop.cpp
```

### 回滚阶段 4 (Few-Shot)
```bash
git checkout HEAD -- src/core/agent_loop.cpp
```

---

## 成功标准

### 阶段 1 成功标准
- ✅ 日志显示 LLM 使用的搜索关键词
- ✅ 日志显示 gh CLI 返回的原始数据
- ✅ 日志显示格式化后的输出
- ✅ 日志显示最终响应

### 阶段 2 成功标准
- ✅ LLM 使用 "awesome-openclaw-skills" 而不是 "openclaw"
- ✅ 搜索结果包含相关的技能仓库
- ✅ Star 数完全保留 (37,262 而不是 0 或 37k)

### 阶段 3 成功标准
- ✅ 宽泛查询触发相关性验证
- ✅ 自动重试使用更精确的关键词
- ✅ 最终结果与原始查询相关

### 阶段 4 成功标准
- ✅ LLM 学习了示例中的搜索策略
- ✅ 新的查询使用类似的精确关键词
- ✅ 搜索质量整体提升

---

## 时间估算

| 阶段 | 开发时间 | 测试时间 | 总计 |
|------|---------|---------|------|
| 阶段 1: 日志增强 | 1 小时 | 0.5 小时 | 1.5 小时 |
| 阶段 2: 关键词优化 | 0.5 小时 | 1 小时 | 1.5 小时 |
| 阶段 3: 相关性验证 | 2 小时 | 1 小时 | 3 小时 |
| 阶段 4: Few-Shot 示例 | 1.5 小时 | 1 小时 | 2.5 小时 |
| **总计** | **5 小时** | **3.5 小时** | **8.5 小时** |

---

## 下一步行动

### 立即执行 (今天)
1. ✅ 完成深度分析报告
2. ⏳ 实施阶段 1: 添加详细日志
3. ⏳ 测试并诊断问题根源

### 短期执行 (本周)
1. ⏳ 实施阶段 2: 优化搜索关键词提示
2. ⏳ 测试关键词优化效果

### 中期执行 (下周)
1. ⏳ 实施阶段 3: 搜索结果相关性验证
2. ⏳ 测试相关性验证机制

### 长期执行 (未来)
1. ⏳ 实施阶段 4: Few-Shot 示例注入
2. ⏳ 持续优化和监控

---

**文档版本**: 1.0
**创建时间**: 2026-03-15
**负责人**: RavBot 开发团队
