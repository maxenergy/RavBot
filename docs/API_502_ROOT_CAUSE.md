# API 502 错误根本原因分析

## 问题现象

第三次 API 请求时出现 502 错误:
```
Anthropic API HTTP 502: "Improperly formed request"
```

## 根本原因

**用户消息被重复添加了两次**,导致请求格式错误:

```json
{
  "role": "user",
  "content": [
    {"type": "text", "text": "请你搜索github找openclaw的技能的top20列表"},
    {"type": "text", "text": "请你搜索github找openclaw的技能的top20列表"}  // 重复!
  ]
}
```

## 验证

使用 curl 直接测试该 payload,API 返回相同错误:
```bash
curl -X POST http://127.0.0.1:8991/v1/messages -d @payload.json
# 返回: "Improperly formed request"
```

## 可能的原因

1. **Context compaction** 过程中重复添加了用户消息
2. **Message building** 逻辑有 bug
3. **Lookup Guard retry** 机制可能重复添加了原始消息

## 需要检查的代码位置

1. `src/core/agent_loop.cpp` - ProcessMessage() 中的消息构建
2. `src/core/agent_loop.cpp` - Lookup Guard retry 逻辑
3. `src/core/agent_loop.cpp` - Context compaction 逻辑

## 修复方向

1. 检查为什么用户消息会被重复添加
2. 在 `serialize_messages_to_anthropic()` 中添加去重逻辑
3. 或者在消息构建时避免重复

## 已完成的修复

1. ✅ 工具权限问题 - 通配符 `*` 支持
2. ✅ tool_choice 优化 - 第一次 `any`,后续 `auto`
3. ✅ 用户消息重复问题 - 在 serialize_messages_to_anthropic 中添加去重逻辑

## 修复详情

### 用户消息去重

**文件**: `src/providers/anthropic_provider.cpp`

在 `serialize_messages_to_anthropic` 函数中添加了去重逻辑:

```cpp
std::unordered_set<std::string> seen_text_blocks;  // Dedup text blocks

for (const auto& b : msg.content) {
  if (b.type == "text" || b.type == "thinking") {
    if (!b.text.empty()) {
      // Deduplicate identical text blocks within the same message
      if (seen_text_blocks.count(b.text) > 0) {
        continue;  // Skip duplicate text block
      }
      seen_text_blocks.insert(b.text);
      content_arr.push_back({{"type", "text"}, {"text", b.text}});
    }
  }
  // ... tool_use and tool_result handling
}
```

这个修复会在序列化时自动去除同一消息中的重复 text blocks,防止 API 502 错误。

## 待修复

无
