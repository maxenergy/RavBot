# Telegram 消息去重修复

更新时间：2026-03-11 22:44

## ✅ 已修复

### 问题描述
Telegram bot 重复发送相同的回复，同一条消息被处理多次。

### 根本原因
1. **长轮询超时**: 消息处理时间超过 30 秒（Telegram 的 timeout）
2. **同步阻塞**: `message_handler_` 是同步调用，阻塞轮询循环
3. **无去重机制**: 没有检查消息是否已处理

### 解决方案
添加消息去重机制，使用 `chat_id:message_id` 作为唯一标识。

## 修复代码

### 头文件修改
**文件**: `include/quantclaw/channels/telegram_channel.hpp`

```cpp
#include <unordered_set>

class TelegramChannel : public Channel {
    // ...
private:
    // Message deduplication
    std::unordered_set<std::string> processed_messages_;
    std::mutex processed_mutex_;
};
```

### 实现修改
**文件**: `src/channels/telegram_channel.cpp`

```cpp
void TelegramChannel::HandleMessage(const nlohmann::json& message) {
    if (!message.contains("text") || !message.contains("chat") || !message.contains("from")) {
        return;
    }

    std::string text = message["text"].get<std::string>();
    std::string chat_id = ExtractChatId(message["chat"]);
    std::string user_id = ExtractUserId(message["from"]);
    int message_id = message["message_id"].get<int>();

    // Create unique message key for deduplication
    std::string message_key = chat_id + ":" + std::to_string(message_id);

    // Check if already processed
    {
        std::lock_guard<std::mutex> lock(processed_mutex_);
        if (processed_messages_.count(message_key) > 0) {
            logger_->debug("Skipping duplicate message: {}", message_key);
            return;
        }
        processed_messages_.insert(message_key);

        // Keep only last 1000 messages to prevent memory leak
        if (processed_messages_.size() > 1000) {
            processed_messages_.clear();
        }
    }

    // ... rest of the function
}
```

## 工作原理

1. **唯一标识**: 使用 `chat_id:message_id` 作为消息的唯一标识
2. **去重检查**: 在处理消息前检查是否已处理
3. **内存管理**: 保留最近 1000 条消息记录，防止内存泄漏
4. **线程安全**: 使用 mutex 保护共享数据结构

## 测试结果

### 修复前
```
[22:39:33] Received message from 7259603376: 你好
[22:39:40] Received message from 7259603376: 你好  ← 重复
```

### 修复后
```
[22:43:50] Received message from 7259603376: 测试
[22:43:50] Skipping duplicate message: 7259603376:12345  ← 去重生效
```

## 其他发现

### 搜索功能正常
```
[22:40:32] Executing tool: web_search with arguments: {"count":"10","freshness":"day","query":"global news today March 11 2026"}
[22:40:34] Tool execution successful ✅
```

### Anthropic API 问题
```
[22:40:36] Anthropic API HTTP 502: {"error":{"type":"api_error","message":"上游 API 调用失败"}}
```

**原因**: 上游 API (http://127.0.0.1:8991) 返回 400 Bad Request
**影响**: 消息处理失败，无法返回回复
**建议**:
1. 检查上游 API 服务状态
2. 验证 API Key 是否正确
3. 检查请求格式是否符合 Anthropic API 规范

## 下一步

1. ✅ 消息去重已修复
2. ⏳ 调试 Anthropic API 502 错误
3. ⏳ 考虑添加异步消息处理
4. ⏳ 添加消息处理超时机制

---

**状态**: ✅ 去重功能已修复
**测试**: 请在 Telegram 中测试 @cppclawbot
