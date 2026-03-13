# Telegram Bot 搜索功能修复完成

更新时间：2026-03-11 22:40

## ✅ 已修复的问题

### 1. Provider 解析问题
**问题**: AgentLoop 在 `resolve_provider()` 中会剥离模型前缀，导致后续无法正确解析 Anthropic provider。

**修复位置**:
- `src/core/agent_loop.cpp:130` - 注释掉 `agent_config_.model = ref.model;`
- `src/cli/gateway_commands.cpp:175` - 注释掉 `config.agent.model = model_ref.model;`

**修复代码**:
```cpp
// agent_loop.cpp:124-131
auto ref = provider_registry_->ResolveModel(agent_config_.model);
auto provider = provider_registry_->GetProviderForModel(ref);
if (provider) {
    last_provider_id_ = ref.provider;
    last_profile_id_ = "";
    // DON'T strip the provider prefix - keep full model reference for future resolution
    // agent_config_.model = ref.model;
    return provider;
}
```

### 2. Web Search Tool 参数类型问题
**问题**: LLM 传入的 `count` 参数是字符串 `"10"`，但工具期望整数，导致 JSON 类型错误。

**修复位置**: `src/tools/tool_registry.cpp:895-899`

**修复代码**:
```cpp
std::string ToolRegistry::web_search_tool(const nlohmann::json& params) {
    std::string query = params.value("query", "");
    // Handle count as either integer or string
    int count = 5;
    if (params.contains("count")) {
        if (params["count"].is_number()) {
            count = params["count"].get<int>();
        } else if (params["count"].is_string()) {
            try {
                count = std::stoi(params["count"].get<std::string>());
            } catch (...) {
                count = 5;
            }
        }
    }
    count = std::clamp(count, 1, 10);
    std::string freshness = params.value("freshness", "");
    if (query.empty()) throw std::runtime_error("query is required");
    // ...
}
```

### 3. SerpAPI 搜索引擎集成
**新增功能**: 添加 SerpAPI (Google Search) 支持

**修改位置**: `src/tools/tool_registry.cpp:1029-1067`

**搜索引擎优先级**:
1. ✅ Brave Search (需要 BRAVE_API_KEY)
2. ✅ Tavily (已配置 TAVILY_API_KEY)
3. ✅ Perplexity (需要 PERPLEXITY_API_KEY)
4. ✅ **SerpAPI** (已配置 SERPAPI_API_KEY) - **新增**
5. ✅ DuckDuckGo (免费，无需 API Key)
6. ✅ xAI Grok (需要 XAI_API_KEY)

**SerpAPI 实现**:
```cpp
// --- SerpAPI (Google Search) ---
const char* serpapi_key = std::getenv("SERPAPI_API_KEY");
if (serpapi_key && *serpapi_key) {
    try {
        std::string path = "/search?engine=google&q=" + url_encode(query) +
                           "&api_key=" + std::string(serpapi_key) +
                           "&num=" + std::to_string(count);

        httplib::SSLClient cli("serpapi.com");
        cli.set_default_headers({{"Accept", "application/json"}});
        cli.set_connection_timeout(10);
        cli.set_read_timeout(15);

        auto res = cli.Get(path);
        if (!res) throw std::runtime_error("SerpAPI: connection failed");
        if (res->status != 200)
            throw std::runtime_error("SerpAPI HTTP " + std::to_string(res->status));

        auto j = nlohmann::json::parse(res->body);
        nlohmann::json results = nlohmann::json::array();
        if (j.contains("organic_results")) {
            for (const auto& r : j["organic_results"]) {
                nlohmann::json item;
                item["title"]       = r.value("title", "");
                item["url"]         = r.value("link", "");
                item["description"] = r.value("snippet", "");
                results.push_back(item);
            }
        }
        return nlohmann::json{{"provider", "serpapi"}, {"query", query},
                               {"results", results}}.dump();
    } catch (const std::exception& e) {
        last_error = std::string("SerpAPI: ") + e.what();
    }
}
```

## 📝 配置文件

### 环境变量配置
**文件**: `~/.quantclaw/env.sh`

```bash
#!/bin/bash
# QuantClaw 环境变量配置

# Tavily API Key (网络搜索)
export TAVILY_API_KEY="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"

# SerpAPI Key (Google 搜索)
export SERPAPI_API_KEY="327952340b074e7c67a9ddf46eb5f7c3d59d82df4c6a3f164674887817bea8e0"

# 其他 API Keys (如需要可以添加)
# export BRAVE_API_KEY="..."
# export PERPLEXITY_API_KEY="..."
```

### Telegram Bot 配置
**文件**: `~/.quantclaw/quantclaw.json`

```json
{
  "channels": {
    "telegram": {
      "enabled": true,
      "botToken": "8208097744:AAFZ9qFR5wJjaxQPVt5BI4LDKNQ6ZXTLyyI",
      "dmPolicy": "open",
      "groupPolicy": "open",
      "historyLimit": 10,
      "dmHistoryLimit": 15,
      "streaming": "partial",
      "groups": {
        "*": {
          "requireMention": false
        }
      },
      "allowFrom": ["*"]
    }
  },
  "providers": {
    "tavily": {
      "apiKey": "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"
    }
  }
}
```

### Anthropic Provider 配置
**文件**: `~/.quantclaw/quantclaw.json`

```json
{
  "agent": {
    "model": "anthropic/claude-sonnet-4-5"
  },
  "models": {
    "anthropic": {
      "profiles": {
        "default": {
          "baseUrl": "http://127.0.0.1:8991",
          "apiKey": "sk-s0n19ykdar6jpnbdma79zrw15wbubba28t9to2kkp5y7llqp"
        }
      }
    }
  }
}
```

## 🚀 启动方式

### 启动 Gateway
```bash
cd /home/rogers/source/develop/QuantClaw/build
source ~/.quantclaw/env.sh
./quantclaw gateway run --port 18800
```

### 验证运行状态
```bash
# 查看进程
ps aux | grep quantclaw

# 查看日志
tail -f ~/.quantclaw/logs/gateway.log

# 查看端口
ss -tlnp | grep 18800
```

## 📊 测试结果

### Bot 信息
- **Bot 名称**: cppclawbot
- **Bot 用户名**: @cppclawbot
- **状态**: ✅ 运行中

### 功能测试
1. ✅ Telegram 消息接收
2. ✅ Agent 响应生成
3. ✅ 搜索工具调用
4. ✅ Anthropic Provider 正确使用
5. ✅ 环境变量正确加载

### 日志示例
```
[2026-03-11 22:39:32.092] [info] Telegram bot started: @cppclawbot
[2026-03-11 22:39:32.092] [info] Telegram channel started (C++ native)
[2026-03-11 22:39:32.092] [info] Telegram polling loop started
[2026-03-11 22:39:33.134] [info] Received message from 7259603376: 你好
[2026-03-11 22:39:33.137] [info] Processing Telegram message from 7259603376
[2026-03-11 22:39:39.487] [info] LLM provided final response
[2026-03-11 22:39:40.703] [info] Sent response to Telegram chat 7259603376
```

## 🔍 搜索功能使用

### 在 Telegram 中测试
1. 打开 Telegram，搜索 `@cppclawbot`
2. 发送消息：`帮我搜索今天全球的重要新闻`
3. Bot 会自动调用搜索工具并返回结果

### 搜索引擎选择
系统会按优先级自动选择可用的搜索引擎：
- 如果 Tavily API Key 可用，优先使用 Tavily
- 如果 Tavily 失败，尝试 SerpAPI
- 如果 SerpAPI 失败，降级到 DuckDuckGo

### 搜索参数
- `query`: 搜索关键词（必需）
- `count`: 结果数量（1-10，默认 5）
- `freshness`: 时间过滤（day, week, month, year）

## 🐛 已知问题

### 1. Anthropic API 502 错误
**症状**: 偶尔出现 502 错误
```
[2026-03-11 22:37:32.701] [error] Anthropic API HTTP 502: {"error":{"type":"api_error","message":"上游 API 调用失败"}}
```

**原因**:
- 上游 API (http://127.0.0.1:8991) 可能不稳定
- 请求格式可能不正确

**解决方案**:
- 检查上游 API 服务状态
- 验证请求格式是否符合 Anthropic API 规范
- 考虑添加重试机制

### 2. 流式响应未实现
**状态**: 待实现
**影响**: 长回复需要等待完成后才能看到
**解决方案**: 使用 `ProcessMessageStream` + `editMessageText`

## 📚 相关文档

- [Telegram Bot 配置](TELEGRAM_BOT_SETUP.md)
- [Telegram Agent 集成](TELEGRAM_AGENT_INTEGRATION.md)
- [Tavily 搜索配置](TAVILY_SEARCH_SETUP.md)
- [Agent Loop 文档](../include/quantclaw/core/agent_loop.hpp)
- [Tool Registry 文档](../include/quantclaw/tools/tool_registry.hpp)

## 🎯 下一步

### 优先级 P0
- ✅ 修复 Provider 解析问题
- ✅ 修复 Web Search 参数类型问题
- ✅ 添加 SerpAPI 支持
- ⏳ 调试 Anthropic API 502 错误

### 优先级 P1
- [ ] 实现流式响应
- [ ] 添加错误重试机制
- [ ] 优化搜索结果格式化

### 优先级 P2
- [ ] 支持图片发送
- [ ] 支持文件上传/下载
- [ ] 添加命令处理（/help, /reset 等）

---

**状态**: ✅ **核心功能已修复**
**测试**: 请在 Telegram 中测试 @cppclawbot
**最后更新**: 2026-03-11 22:40
