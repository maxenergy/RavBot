# Tavily 搜索配置完成

配置时间：2026-03-11 22:25

## ✅ 配置状态

### API Key
- **Provider**: Tavily
- **API Key**: `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`
- **状态**: ✅ 已配置

### 配置位置
1. **配置文件**: `~/.quantclaw/quantclaw.json`
   ```json
   {
     "providers": {
       "tavily": {
         "apiKey": "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"
       }
     }
   }
   ```

2. **环境变量**: `~/.quantclaw/env.sh`
   ```bash
   export TAVILY_API_KEY="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"
   ```

## 🔍 搜索功能

### 搜索引擎优先级
1. ✅ **Tavily** (已配置) - 高质量 AI 搜索
2. ⏸️ Brave Search (未配置)
3. ⏸️ Perplexity (未配置)
4. ✅ **DuckDuckGo** (免费后备) - 始终可用
5. ⏸️ xAI Grok (未配置)

### 使用方法

#### 1. 在 Telegram 中直接提问
```
你：搜索一下最新的 AI 新闻
Bot：[自动调用 Tavily 搜索并返回结果]
```

#### 2. 使用 /search 命令
```
/search Python 3.12 新特性
```

#### 3. 在对话中自然提问
```
你：今天的天气怎么样？
Bot：[自动搜索天气信息]

你：最近有什么科技新闻？
Bot：[自动搜索科技新闻]
```

## 📊 Tavily 特性

### 优势
- ✅ 专为 AI 应用优化
- ✅ 高质量搜索结果
- ✅ 支持实时信息
- ✅ 快速响应
- ✅ 结构化数据

### 配额
- **免费额度**: 1,000 次搜索/月
- **响应时间**: ~1-2 秒
- **结果质量**: ⭐⭐⭐⭐⭐

### 支持的参数
```json
{
  "query": "搜索内容",
  "count": 5,           // 结果数量 (1-10)
  "freshness": "day"    // 时间过滤: day, week, month, year
}
```

## 🧪 测试示例

### 示例 1：基本搜索
**问题**: "搜索 Claude AI 的最新功能"
**预期**: Tavily 返回 Claude AI 相关的最新信息

### 示例 2：时效性搜索
**问题**: "今天的科技新闻"
**预期**: Tavily 返回当天的科技新闻（freshness=day）

### 示例 3：技术文档
**问题**: "Rust async 编程教程"
**预期**: Tavily 返回高质量的 Rust 教程链接

## 🔧 故障排查

### 1. 搜索无结果
**检查**:
```bash
# 验证环境变量
echo $TAVILY_API_KEY

# 查看日志
./quantclaw logs | grep -i tavily
```

### 2. API Key 错误
**症状**: 返回 401 或 403 错误
**解决**:
- 检查 API Key 是否正确
- 验证配额是否用完
- 访问 https://tavily.com 查看账户状态

### 3. 搜索超时
**症状**: 请求超时
**解决**:
- 自动降级到 DuckDuckGo
- 检查网络连接
- 查看 Tavily 服务状态

## 📝 使用建议

### 最佳实践
1. **具体的查询**: "Python 3.12 新特性" 比 "Python" 更好
2. **时间限定**: 使用 freshness 参数获取最新信息
3. **结果数量**: 默认 5 个结果通常足够
4. **自然语言**: 可以用自然语言提问，Agent 会自动优化查询

### 注意事项
- ⚠️ 每月 1000 次免费额度，注意使用量
- ⚠️ 搜索结果会消耗 token，影响对话长度
- ⚠️ 实时信息可能有延迟（通常 <1 小时）

## 🚀 下一步

### 立即测试
现在可以在 Telegram 中测试搜索功能：

1. 打开 Telegram，找到 @cppclawbot
2. 发送消息："搜索一下最新的 AI 新闻"
3. Bot 会使用 Tavily 进行搜索并返回结果

### 可选配置
如果需要更多搜索选项，可以添加：

```bash
# Brave Search (高质量，需要付费)
export BRAVE_API_KEY="..."

# Perplexity (AI 搜索)
export PERPLEXITY_API_KEY="..."
```

## 📚 相关文档

- [Tavily 官网](https://tavily.com)
- [Tavily API 文档](https://docs.tavily.com)
- [QuantClaw 搜索工具](../src/tools/tool_registry.cpp:892-1160)

---

**状态**: ✅ 配置完成
**搜索引擎**: Tavily (主) + DuckDuckGo (后备)
**测试**: 请在 Telegram 中测试
