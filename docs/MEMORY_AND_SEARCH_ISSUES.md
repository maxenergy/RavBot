# 记忆系统和搜索功能问题报告

**日期**: 2026-03-15
**状态**: 🔴 多个关键问题
**优先级**: P0

---

## 问题概述

发现了三个相关的关键问题：

1. **向量数据库为空** - 缺少自动索引机制
2. **长效记忆文件为空** - 没有内容被写入
3. **搜索功能报错** - API 请求格式错误导致 502

---

## 问题 1: 向量数据库为空

### 症状

```bash
$ sqlite3 ~/.ravbot/data/vectors.db "SELECT COUNT(*) FROM vectors;"
0
```

### 根本原因

向量搜索基础设施已完整实现（HNSW + Ollama + EmbeddingManager），但**缺少自动索引机制**：

- ❌ `IndexText` 从未在生产代码中被调用
- ❌ `AgentLoop` 未集成 `EmbeddingManager`
- ❌ 消息处理流程中没有索引钩子

### 解决方案

在 `AgentLoop` 中集成 `EmbeddingManager`，自动索引所有消息。

**详细分析**: 见 `docs/VECTOR_INDEXING_GAP_ANALYSIS.md`

**工作量**: 4.5 小时

---

## 问题 2: 长效记忆文件为空

### 症状

```bash
$ cat ~/.ravbot/agents/main/workspace/MEMORY.md
# Memory

This file is used to store persistent memory across conversations.
The agent will read and update this file to remember important information.

## Key Facts
<!-- The agent will add important facts and context here -->

## Preferences
<!-- User preferences and working style -->
```

### 根本原因

长效记忆文件只有模板结构，没有实际内容。可能的原因：

1. **Agent 没有被指示写入记忆** - System prompt 中可能缺少记忆写入指令
2. **记忆写入工具缺失** - 可能没有 `memory_write` 或 `memory_update` 工具
3. **自动记忆机制缺失** - 没有自动提取和保存重要信息的机制

### 当前记忆系统

RavBot 有两套记忆系统：

1. **BM25 搜索** (`MemorySearch`) - 基于关键词的全文搜索
   - ✅ 已实现
   - ✅ 可以搜索 workspace 中的所有文件
   - ❌ 但 workspace 文件大多为空

2. **向量搜索** (`EmbeddingManager` + `VectorDatabase`) - 基于语义的向量搜索
   - ✅ 已实现
   - ❌ 未集成到 Agent Loop
   - ❌ 向量数据库为空

### 检查记忆工具

让我检查是否有记忆写入工具：

```bash
$ grep -r "memory_write\|memory_update\|memory_add" src/tools/tool_registry.cpp
# 结果: 无匹配
```

**发现**: 只有 `memory_search` 和 `memory_get` 工具，**没有记忆写入工具**！

### 对比 OpenClaw

OpenClaw 有以下记忆相关工具：
- `memory_search` - 搜索记忆 ✅ RavBot 有
- `memory_get` - 获取记忆文件 ✅ RavBot 有
- `memory_write` - 写入记忆 ❌ RavBot 缺失
- `memory_update` - 更新记忆 ❌ RavBot 缺失
- `memory_append` - 追加记忆 ❌ RavBot 缺失

### 解决方案

#### 方案 A: 添加记忆写入工具 (推荐 ⭐⭐⭐⭐⭐)

**实施步骤**:

1. **添加 `memory_write` 工具**

```cpp
// src/tools/tool_registry.cpp
register_tool("memory_write",
    "Write or update content in agent memory files (MEMORY.md, etc.)",
    {
        {"path", "string", "Relative path to memory file (e.g., 'MEMORY.md')"},
        {"content", "string", "Content to write"},
        {"mode", "string", "Write mode: 'overwrite' or 'append' (default: 'overwrite')"}
    },
    [this](const nlohmann::json& p) { return memory_write_tool(p); });
```

2. **实现 `memory_write_tool`**

```cpp
std::string ToolRegistry::memory_write_tool(const nlohmann::json& params) {
    std::string rel_path = params.value("path", "");
    std::string content = params.value("content", "");
    std::string mode = params.value("mode", "overwrite");

    if (rel_path.empty()) throw std::runtime_error("path is required");
    if (content.empty()) throw std::runtime_error("content is required");

    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "/tmp";
    auto workspace = std::filesystem::path(home_str) / ".ravbot/agents/main/workspace";
    auto full_path = workspace / rel_path;

    // Security: must remain inside workspace
    auto canonical = std::filesystem::weakly_canonical(full_path);
    auto ws_canon = std::filesystem::weakly_canonical(workspace);
    if (canonical.string().substr(0, ws_canon.string().size()) != ws_canon.string()) {
        throw std::runtime_error("Access denied: path outside workspace");
    }

    // Write or append
    std::ios_base::openmode open_mode = std::ios::out;
    if (mode == "append") {
        open_mode |= std::ios::app;
    }

    std::ofstream ofs(full_path, open_mode);
    if (!ofs) {
        throw std::runtime_error("Failed to open file: " + rel_path);
    }

    ofs << content;
    if (mode == "append") {
        ofs << "\n";
    }

    logger_->info("Wrote {} bytes to {}", content.size(), rel_path);

    return nlohmann::json{
        {"success", true},
        {"path", rel_path},
        {"bytes", content.size()},
        {"mode", mode}
    }.dump();
}
```

3. **更新 System Prompt**

在 `src/core/prompt_builder.cpp` 中添加记忆写入指令：

```cpp
rules << "**Memory Management:**\n\n";
rules << "- Use `memory_write` to save important information to MEMORY.md\n";
rules << "- Save user preferences, key facts, and important context\n";
rules << "- Update memory when you learn new information about the user\n";
rules << "- Use `memory_search` to recall previously saved information\n\n";
```

**工作量**: 2-3 小时

#### 方案 B: 自动记忆提取

实现自动提取和保存重要信息的机制。

**工作量**: 5-6 小时

---

## 问题 3: 搜索功能报错 (HTTP 502)

### 症状

用户请求: "请帮我搜索视频创作的技能"

错误日志:
```
[2026-03-15 17:09:05.509] [error] Anthropic API HTTP 502: {"error":{"type":"api_error","message":"上游 API 调用失败: 非流式 API 请求失败: 400 Bad Request {\"message\":\"Improperly formed request.\",\"reason\":null}"}}
```

### 问题链

1. **用户请求**: "请帮我搜索视频创作的技能"
2. **LLM 调用**: `github_search_repos` 工具
3. **工具失败**: gh CLI 未安装或未认证
   ```
   Error searching GitHub repositories: Command exited 1:
   Make sure gh CLI is installed and authenticated (run: gh auth login)
   ```
4. **错误返回**: tool_result 包含错误信息
5. **LLM 再次调用**: `web_search` 工具
6. **web_search 返回大量内容** (可能超过 token 限制)
7. **API 请求失败**: HTTP 502 - Improperly formed request

### 根本原因

可能的原因：

1. **请求过大** - web_search 返回的内容太多，导致请求超过 API 限制
2. **格式错误** - tool_result 中的内容格式不正确
3. **特殊字符** - 返回内容中包含未转义的特殊字符

### 解决方案

#### 方案 A: 限制 web_search 返回内容大小

```cpp
// src/tools/tool_registry.cpp - web_search_tool
std::string ToolRegistry::web_search_tool(const nlohmann::json& params) {
    // ... 现有逻辑 ...

    // 限制每个结果的内容大小
    const size_t MAX_CONTENT_SIZE = 500;  // 每个结果最多 500 字符
    const size_t MAX_TOTAL_SIZE = 5000;   // 总共最多 5000 字符

    size_t total_size = 0;
    for (auto& result : results) {
        if (result.content.size() > MAX_CONTENT_SIZE) {
            result.content = result.content.substr(0, MAX_CONTENT_SIZE) + "...";
        }
        total_size += result.content.size();
        if (total_size > MAX_TOTAL_SIZE) {
            break;  // 停止添加更多结果
        }
    }

    // ... 返回结果 ...
}
```

#### 方案 B: 改进错误处理

```cpp
// src/tools/tool_registry.cpp - github_search_repos_tool
std::string ToolRegistry::github_search_repos_tool(const nlohmann::json& params) {
    try {
        // ... 现有逻辑 ...
    } catch (const std::exception& e) {
        // 返回简短的错误信息，避免包含完整的错误堆栈
        logger_->error("GitHub search failed: {}", e.what());
        return nlohmann::json{
            {"error", "GitHub CLI not available. Please install and authenticate gh CLI."},
            {"results", nlohmann::json::array()}
        }.dump();
    }
}
```

#### 方案 C: 添加 gh CLI 检查

在启动时检查 gh CLI 是否可用，如果不可用则禁用相关工具。

```cpp
// src/main.cpp
bool check_gh_cli() {
    int ret = std::system("gh --version > /dev/null 2>&1");
    return ret == 0;
}

// 在初始化 ToolRegistry 时
if (!check_gh_cli()) {
    logger->warn("gh CLI not available, disabling GitHub tools");
    tool_registry->DisableTool("github_search_repos");
    tool_registry->DisableTool("github_search_code");
    // ... 其他 GitHub 工具
}
```

**工作量**: 1-2 小时

---

## 优先级排序

### P0 (立即修复)

1. **添加记忆写入工具** (2-3 小时)
   - 影响: 长效记忆功能完全不可用
   - 风险: 低
   - 价值: 高

2. **修复搜索功能报错** (1-2 小时)
   - 影响: 用户体验差，搜索功能不稳定
   - 风险: 低
   - 价值: 高

### P1 (短期修复)

3. **集成向量索引到 Agent Loop** (4.5 小时)
   - 影响: 语义搜索功能不可用
   - 风险: 低
   - 价值: 中

### P2 (中期优化)

4. **自动记忆提取** (5-6 小时)
   - 影响: 记忆管理需要手动操作
   - 风险: 中
   - 价值: 中

---

## 实施计划

### Phase 1: 记忆写入工具 (2-3 小时)

1. ✅ 添加 `memory_write` 工具到 ToolRegistry
2. ✅ 实现 `memory_write_tool` 方法
3. ✅ 更新 System Prompt 添加记忆管理指令
4. ✅ 测试记忆写入功能

### Phase 2: 搜索功能修复 (1-2 小时)

5. ✅ 限制 web_search 返回内容大小
6. ✅ 改进 GitHub 工具错误处理
7. ✅ 添加 gh CLI 可用性检查
8. ✅ 测试搜索功能

### Phase 3: 向量索引集成 (4.5 小时)

9. ✅ 修改 `agent_loop.hpp` - 添加 EmbeddingManager 成员
10. ✅ 修改 `agent_loop.cpp` - 添加索引逻辑
11. ✅ 修改 `main.cpp` - 更新初始化
12. ✅ 添加配置项 `auto_index_messages`
13. ✅ 测试向量索引功能

**总工作量**: 7.5-10.5 小时

---

## 风险评估

### 低风险 ✅

- 记忆写入工具 - 新增功能，不影响现有功能
- 搜索功能修复 - 改进错误处理，降低失败率
- 向量索引集成 - 可配置开关，易于回滚

### 需要注意 ⚠️

- 记忆文件安全性 - 需要严格的路径验证
- web_search 内容截断 - 可能影响搜索结果质量
- 向量索引性能 - 需要监控索引开销

---

## 后续优化

### 短期 (1-2 周)

1. **智能记忆提取** - 自动识别和保存重要信息
2. **记忆搜索优化** - 结合 BM25 和向量搜索
3. **工具可用性检测** - 自动检测和禁用不可用的工具

### 中期 (1-2 月)

4. **混合搜索** - BM25 + 向量搜索的混合排序
5. **记忆压缩** - 自动压缩和归档旧记忆
6. **多模态记忆** - 支持图片、代码片段等

### 长期 (3-6 月)

7. **分布式记忆** - 支持多 Agent 共享记忆
8. **记忆推理** - 基于记忆的推理和推荐
9. **记忆可视化** - 记忆图谱和关系可视化

---

## 总结

### 问题本质

1. **向量数据库为空** - 缺少自动索引机制
2. **长效记忆文件为空** - 缺少记忆写入工具
3. **搜索功能报错** - 工具错误处理不完善 + 内容大小限制

### 解决方案

1. **添加记忆写入工具** - 允许 Agent 写入和更新记忆
2. **修复搜索功能** - 改进错误处理和内容大小限制
3. **集成向量索引** - 在 Agent Loop 中自动索引消息

### 预期效果

- ✅ 长效记忆功能可用
- ✅ 搜索功能稳定
- ✅ 语义搜索功能可用
- ✅ 用户体验显著提升

### 下一步

立即实施 Phase 1 和 Phase 2，预计 3-5 小时完成。

---

**报告完成时间**: 2026-03-15 20:30 UTC
**负责人**: team-lead
**状态**: 待实施
