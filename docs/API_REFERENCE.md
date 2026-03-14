# QuantClaw API Reference

## 概述

QuantClaw 提供了完整的 C++ API 用于构建 AI Agent 应用。本文档涵盖了核心类和方法的使用说明。

## 核心模块

### 1. Agent Loop (核心执行循环)

#### AgentLoop

Agent 执行循环的核心类,负责处理用户请求并协调各个组件。

**头文件**: `quantclaw/core/agent_loop.hpp`

**主要方法**:

```cpp
// 处理用户请求
AgentResponse ProcessRequest(const AgentRequest& request);

// 中止正在执行的请求
void AbortRequest(const std::string& request_id);

// 获取执行统计信息
AgentStats GetStats() const;
```

**使用示例**:

```cpp
#include "quantclaw/core/agent_loop.hpp"

auto memory_manager = std::make_shared<MemoryManager>(workspace_dir, logger);
auto skill_loader = std::make_shared<SkillLoader>(logger);
auto tool_registry = std::make_shared<ToolRegistry>(logger);
auto provider = std::make_shared<AnthropicProvider>(api_key, logger);

AgentLoop agent_loop(memory_manager, skill_loader, tool_registry,
                     provider, config.agent, logger);

AgentRequest request;
request.message = "Hello, how can you help me?";
request.session_id = "my-session";

AgentResponse response = agent_loop.ProcessRequest(request);
std::cout << response.content << std::endl;
```

### 2. Memory Manager (内存管理)

#### MemoryManager

管理 Agent 的工作空间和文件系统操作。

**头文件**: `quantclaw/core/memory_manager.hpp`

**主要方法**:

```cpp
// 读取文件
std::string ReadFile(const std::string& path);

// 写入文件
void WriteFile(const std::string& path, const std::string& content);

// 列出目录
std::vector<std::string> ListDirectory(const std::string& path);

// 搜索文件内容
std::vector<SearchResult> SearchFiles(const std::string& pattern);
```

### 3. Session Manager (会话管理)

#### SessionManager

管理用户会话和对话历史。

**头文件**: `quantclaw/session/session_manager.hpp`

**主要方法**:

```cpp
// 创建新会话
void CreateSession(const std::string& session_id);

// 获取会话
Session GetSession(const std::string& session_id);

// 保存会话
void SaveSession(const std::string& session_id, const Session& session);

// 删除会话
void DeleteSession(const std::string& session_id);

// 列出所有会话
std::vector<std::string> ListSessions();
```

### 4. Provider Registry (LLM 提供商)

#### ProviderRegistry

管理多个 LLM 提供商并支持故障转移。

**头文件**: `quantclaw/providers/provider_registry.hpp`

**主要方法**:

```cpp
// 注册提供商
void RegisterProvider(const std::string& name,
                     std::shared_ptr<LLMProvider> provider);

// 获取提供商
std::shared_ptr<LLMProvider> GetProvider(const std::string& name);

// 设置主提供商
void SetPrimaryProvider(const std::string& name);

// 添加备用提供商
void AddFallbackProvider(const std::string& name);

// 获取故障转移统计
FailoverStats GetFailoverStats() const;
```

**使用示例**:

```cpp
auto registry = std::make_shared<ProviderRegistry>(logger);

// 注册 Anthropic 提供商
auto anthropic = std::make_shared<AnthropicProvider>(api_key, logger);
registry->RegisterProvider("anthropic", anthropic);

// 注册 OpenAI 提供商作为备用
auto openai = std::make_shared<OpenAIProvider>(api_key, logger);
registry->RegisterProvider("openai", openai);

// 配置故障转移
registry->SetPrimaryProvider("anthropic");
registry->AddFallbackProvider("openai");
```

### 5. Tool Registry (工具注册)

#### ToolRegistry

管理可用的工具和函数调用。

**头文件**: `quantclaw/tools/tool_registry.hpp`

**主要方法**:

```cpp
// 注册内置工具
void RegisterBuiltinTools();

// 注册自定义工具
void RegisterTool(const std::string& name, ToolFunction func);

// 执行工具
ToolResult ExecuteTool(const std::string& name, const nlohmann::json& params);

// 列出所有工具
std::vector<std::string> ListTools() const;
```

### 6. Gateway Server (网关服务器)

#### GatewayServer

WebSocket RPC 服务器,提供远程访问接口。

**头文件**: `quantclaw/gateway/gateway_server.hpp`

**主要方法**:

```cpp
// 启动服务器
void Start();

// 停止服务器
void Stop();

// 注册 RPC 方法
void RegisterMethod(const std::string& method, RpcHandler handler);

// 设置认证
void SetAuth(const std::string& mode, const std::string& token);

// 广播事件
void BroadcastEvent(const std::string& event, const nlohmann::json& data);
```

**使用示例**:

```cpp
auto server = std::make_unique<GatewayServer>(port, logger);
server->SetAuth("token", "my-secret-token");

// 注册自定义 RPC 方法
server->RegisterMethod("custom.hello", [](const nlohmann::json& params) {
    nlohmann::json result;
    result["message"] = "Hello, " + params["name"].get<std::string>();
    return result;
});

server->Start();
```

### 7. Gateway Client (网关客户端)

#### GatewayClient

WebSocket RPC 客户端,用于连接到 Gateway Server。

**头文件**: `quantclaw/gateway/gateway_client.hpp`

**主要方法**:

```cpp
// 连接到服务器
bool Connect(int timeout_ms = 5000);

// 断开连接
void Disconnect();

// 调用 RPC 方法
nlohmann::json Call(const std::string& method,
                   const nlohmann::json& params = {});

// 订阅事件
void Subscribe(const std::string& event, EventCallback callback);
```

**使用示例**:

```cpp
auto client = std::make_unique<GatewayClient>(
    "ws://localhost:8765", "my-secret-token", logger);

if (client->Connect()) {
    // 调用 agent.request
    nlohmann::json params = {
        {"message", "Hello"},
        {"session_id", "my-session"}
    };
    auto result = client->Call("agent.request", params);
    std::cout << result["response"] << std::endl;

    client->Disconnect();
}
```

## 配置系统

### QuantClawConfig

全局配置结构。

**头文件**: `quantclaw/config.hpp`

**主要字段**:

```cpp
struct QuantClawConfig {
    struct AgentConfig {
        std::string model;              // LLM 模型名称
        int max_iterations;             // 最大迭代次数
        int max_context_tokens;         // 最大上下文 token 数
        bool enable_context_pruning;    // 启用上下文修剪
    } agent;

    struct GatewayConfig {
        int port;                       // 服务器端口
        struct AuthConfig {
            std::string mode;           // 认证模式: "none", "token"
            std::string token;          // 认证 token
        } auth;
    } gateway;

    struct ProviderConfig {
        std::string primary;            // 主提供商
        std::vector<std::string> fallbacks;  // 备用提供商
        int retry_max_attempts;         // 重试次数
        int retry_delay_ms;             // 重试延迟
    } providers;
};
```

**加载配置**:

```cpp
#include "quantclaw/config.hpp"

// 从文件加载
auto config = QuantClawConfig::LoadFromFile("config.json");

// 从 JSON 加载
nlohmann::json j = /* ... */;
auto config = QuantClawConfig::FromJson(j);

// 验证配置
auto errors = QuantClawConfig::Validate(j);
if (!errors.empty()) {
    for (const auto& error : errors) {
        std::cerr << "Config error: " << error << std::endl;
    }
}
```

## Plugin 系统

### PluginSystem

动态插件加载和管理。

**头文件**: `quantclaw/plugins/plugin_system.hpp`

**主要方法**:

```cpp
// 加载插件
void LoadPlugin(const std::filesystem::path& plugin_dir);

// 卸载插件
void UnloadPlugin(const std::string& plugin_id);

// 列出已加载插件
std::vector<std::string> ListPlugins() const;

// 获取插件信息
PluginManifest GetPluginInfo(const std::string& plugin_id) const;
```

## 安全模块

### SecurityAuditLogger

安全审计日志记录。

**头文件**: `quantclaw/security/audit_logger.hpp`

**主要方法**:

```cpp
// 记录内容包装
void LogContentWrapping(const std::string& source,
                       const std::string& content_preview);

// 记录危险工具调用
void LogDangerousToolCall(const std::string& tool_name,
                         const nlohmann::json& params);

// 记录认证失败
void LogAuthFailure(const std::string& client_id,
                   const std::string& reason);

// 导出日志
std::vector<AuditLogEntry> Export(const ExportOptions& options);
```

## 错误处理

### ProviderError

LLM 提供商错误分类。

**头文件**: `quantclaw/providers/provider_error.hpp`

**错误类型**:

```cpp
enum class ProviderErrorType {
    kRateLimitError,      // 速率限制
    kAuthError,           // 认证错误
    kNetworkError,        // 网络错误
    kServerError,         // 服务器错误
    kInvalidRequest,      // 无效请求
    kContextLengthError,  // 上下文长度超限
    kUnknownError         // 未知错误
};
```

**使用示例**:

```cpp
try {
    auto response = provider->ChatCompletion(request);
} catch (const ProviderError& e) {
    switch (e.type) {
        case ProviderErrorType::kRateLimitError:
            // 处理速率限制
            std::this_thread::sleep_for(std::chrono::seconds(60));
            break;
        case ProviderErrorType::kAuthError:
            // 处理认证错误
            throw;
        default:
            // 其他错误
            break;
    }
}
```

## 最佳实践

### 1. 资源管理

使用 RAII 和智能指针管理资源:

```cpp
auto server = std::make_unique<GatewayServer>(port, logger);
// 自动清理
```

### 2. 错误处理

始终捕获和处理异常:

```cpp
try {
    auto response = agent_loop.ProcessRequest(request);
} catch (const std::exception& e) {
    logger->error("Request failed: {}", e.what());
}
```

### 3. 日志记录

使用 spdlog 进行结构化日志:

```cpp
logger->info("Processing request: session_id={}", session_id);
logger->error("Failed to load config: {}", error_msg);
```

### 4. 线程安全

使用互斥锁保护共享状态:

```cpp
std::lock_guard<std::mutex> lock(mutex_);
// 访问共享数据
```

## 性能优化

### 1. 连接复用

复用 Gateway Client 连接:

```cpp
// 创建一次,多次使用
auto client = std::make_unique<GatewayClient>(url, token, logger);
client->Connect();

for (const auto& request : requests) {
    auto result = client->Call("agent.request", request);
    // 处理结果
}

client->Disconnect();
```

### 2. 批量操作

批量处理请求以提高效率:

```cpp
std::vector<std::future<AgentResponse>> futures;
for (const auto& request : requests) {
    futures.push_back(std::async(std::launch::async, [&]() {
        return agent_loop.ProcessRequest(request);
    }));
}
```

### 3. 上下文管理

启用上下文修剪以控制内存使用:

```cpp
config.agent.enable_context_pruning = true;
config.agent.max_context_tokens = 100000;
```

## 参考链接

- [GitHub Repository](https://github.com/yourusername/quantclaw)
- [配置示例](../config.example.json)
- [性能报告](../tests/performance/PERFORMANCE_REPORT.md)
- [测试报告](../tests/COMPREHENSIVE_TEST_REPORT.md)

## 版本信息

- **当前版本**: 0.3.0
- **API 稳定性**: Beta
- **最后更新**: 2026-03-14
