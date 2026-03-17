# OpenClaw Reference Rule

## 核心原则

**每个任务开始前，请比对参考 OpenClaw 对应的代码进行比较分析再编写 C++ 的复刻代码。**

## 详细规则

### 1. 任务开始前的准备工作

在开始实现任何 RavBot 核心对齐项目的任务之前，必须：

1. **定位 OpenClaw 对应代码**
   - 根据任务描述，在 `/home/rogers/develop/openclaw` 中找到对应的 TypeScript 源文件
   - 参考 `func_logic_diff.md` 中的模块映射关系
   - 使用 `mcp_Augment_Context_Engine_codebase_retrieval` 工具搜索相关代码

2. **深入分析 OpenClaw 实现**
   - 阅读并理解 OpenClaw 的实现逻辑
   - 识别关键的数据结构和算法
   - 理解错误处理和边界条件
   - 注意性能优化和最佳实践
   - 识别 TypeScript 特有的模式和 C++ 的对应实现方式

3. **比较分析**
   - 对比 OpenClaw 和当前 RavBot 的实现差距
   - 识别需要新增的功能
   - 识别需要修改的现有代码
   - 评估向后兼容性影响

### 2. 实现阶段

在编写 C++ 复刻代码时：

1. **保持逻辑对等**
   - 确保 C++ 实现与 OpenClaw 的逻辑行为一致
   - 不要简化或省略 OpenClaw 中的重要逻辑
   - 保持相同的错误处理策略

2. **适配 C++ 语言特性**
   - 使用 C++17 标准特性
   - 遵循 Google C++ 代码风格
   - 使用 RAII、智能指针等 C++ 最佳实践
   - 适当使用 STL 容器和算法

3. **性能考虑**
   - 利用 C++ 的性能优势
   - 避免不必要的内存分配
   - 使用移动语义减少拷贝
   - 考虑多线程安全性

### 3. 验证阶段

实现完成后：

1. **功能验证**
   - 确保实现满足需求文档中的验收标准
   - 对比 OpenClaw 的行为进行测试
   - 验证边界条件和错误情况

2. **代码审查**
   - 检查是否遵循了 OpenClaw 的核心逻辑
   - 确认没有遗漏重要功能
   - 验证 C++ 实现的正确性

## 模块映射参考

根据 `func_logic_diff.md`，以下是主要模块的映射关系：

| RavBot 模块 | OpenClaw 对应模块 |
|---------------|------------------|
| `src/core/prompt_builder.cpp` | `src/agents/system-prompt.ts` |
| `src/core/agent_loop.cpp` | `src/agents/pi-embedded-runner/run.ts` |
| `src/core/turn_validator.cpp` | `src/agents/pi-embedded-helpers/turns.ts` |
| `src/gateway/gateway_server.cpp` | `src/gateway/server-methods/chat.ts` |
| `src/session/session_manager.cpp` | `src/sessions/transcript-events.ts` |
| `src/channels/telegram_channel.cpp` | `src/telegram/bot.ts` |
| `src/plugins/plugin_system.cpp` | `src/plugins/hook-runner-global.ts` |
| `src/security/external_content.cpp` | `src/security/external-content.ts` |
| `src/tools/web_search.cpp` | `src/agents/tools/web-search.ts` |
| `src/providers/failover_resolver.cpp` | `src/providers/failover-resolver.ts` |

## 示例工作流程

### 示例：实现 Prompt Builder 的 build_skills_protocol() 方法

1. **定位 OpenClaw 代码**
   ```bash
   # 在 OpenClaw 项目中查找
   cd /home/rogers/develop/openclaw
   # 查看 system-prompt.ts 中的技能协议部分
   ```

2. **分析 OpenClaw 实现**
   - 阅读 `src/agents/system-prompt.ts` 中技能协议的生成逻辑
   - 理解技能调用格式、参数规范、示例等内容
   - 注意动态内容的生成方式

3. **设计 C++ 实现**
   - 定义相应的数据结构
   - 设计字符串构建逻辑
   - 考虑性能和内存效率

4. **编写 C++ 代码**
   ```cpp
   std::string PromptBuilder::build_skills_protocol() const {
     // 参考 OpenClaw 的逻辑实现
     // 保持相同的输出格式和内容
     // ...
   }
   ```

5. **验证实现**
   - 编写单元测试
   - 对比输出与 OpenClaw 的一致性
   - 验证边界条件

## 注意事项

1. **不要盲目复制**
   - 理解逻辑后再实现，不要直接翻译代码
   - 考虑 C++ 的惯用法和最佳实践

2. **保持灵活性**
   - 如果 OpenClaw 的实现有明显问题，可以改进
   - 但要在设计文档中说明改进原因

3. **文档化差异**
   - 如果 C++ 实现与 OpenClaw 有显著差异，在代码注释中说明
   - 记录设计决策和权衡

4. **持续对比**
   - 在整个实现过程中持续参考 OpenClaw
   - 不要等到实现完成后再对比

## 适用范围

本规则适用于所有 RavBot 核心对齐项目（`.kiro/specs/ravbot-core-alignment/`）的任务实施。
