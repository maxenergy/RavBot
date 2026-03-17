# RavBot Configuration Guide

## 概述

RavBot 使用 JSON5 格式的配置文件,支持注释和灵活的语法。配置文件通常位于 `~/.ravbot/config.json` 或项目根目录的 `config.json`。

## 配置文件结构

### 完整配置示例

```json5
{
  // Agent 配置
  "agent": {
    "model": "claude-sonnet-4-6",
    "max_iterations": 10,
    "max_context_tokens": 200000,
    "enable_context_pruning": true,
    "context_pruning_threshold": 0.8,
    "enable_turn_validation": true
  },

  // LLM 提供商配置
  "providers": {
    "primary": "anthropic",
    "fallbacks": ["openai"],
    "retry_max_attempts": 3,
    "retry_delay_ms": 1000,
    "cooldown_duration_ms": 60000,

    "anthropic": {
      "api_key": "${ANTHROPIC_API_KEY}",
      "base_url": "https://api.anthropic.com",
      "timeout_ms": 120000,
      "max_retries": 3
    },

    "openai": {
      "api_key": "${OPENAI_API_KEY}",
      "base_url": "https://api.openai.com/v1",
      "timeout_ms": 120000,
      "max_retries": 3
    }
  },

  // Gateway 配置
  "gateway": {
    "port": 8765,
    "host": "0.0.0.0",
    "auth": {
      "mode": "token",
      "token": "${GATEWAY_TOKEN}"
    },
    "max_connections": 100,
    "message_size_limit": 10485760
  },

  // 通道配置
  "channels": {
    "telegram": {
      "enabled": true,
      "bot_token": "${TELEGRAM_BOT_TOKEN}",
      "allowed_users": [],
      "dedup_window_seconds": 300,
      "thread_binding_enabled": true
    },

    "slack": {
      "enabled": false,
      "bot_token": "${SLACK_BOT_TOKEN}",
      "app_token": "${SLACK_APP_TOKEN}"
    }
  },

  // 系统配置
  "system": {
    "workspace_dir": "~/.ravbot/workspace",
    "sessions_dir": "~/.ravbot/sessions",
    "plugins_dir": "~/.ravbot/plugins",
    "log_level": "info",
    "log_file": "~/.ravbot/logs/ravbot.log"
  },

  // 安全配置
  "security": {
    "sandbox": {
      "enabled": true,
      "allowed_commands": ["ls", "cat", "grep"],
      "blocked_paths": ["/etc", "/sys", "/proc"]
    },

    "rbac": {
      "enabled": true,
      "default_role": "user"
    },

    "rate_limiter": {
      "enabled": true,
      "requests_per_minute": 60,
      "burst_size": 10
    },

    "audit": {
      "enabled": true,
      "log_dir": "~/.ravbot/logs/audit"
    }
  },

  // MCP 配置
  "mcp": {
    "servers": {
      "filesystem": {
        "command": "npx",
        "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path/to/workspace"]
      }
    }
  },

  // Cron 配置
  "cron": {
    "enabled": true,
    "jobs": [
      {
        "name": "session_cleanup",
        "schedule": "0 2 * * *",
        "command": "cleanup_old_sessions",
        "params": {"days": 30}
      }
    ]
  }
}
```

## 配置项详解

### 1. Agent 配置

#### agent.model
- **类型**: string
- **默认值**: "claude-sonnet-4-6"
- **说明**: LLM 模型名称
- **可选值**:
  - Anthropic: "claude-opus-4-6", "claude-sonnet-4-6", "claude-haiku-4-5"
  - OpenAI: "gpt-4", "gpt-3.5-turbo"

#### agent.max_iterations
- **类型**: integer
- **默认值**: 10
- **范围**: 1-100
- **说明**: Agent 执行循环的最大迭代次数

#### agent.max_context_tokens
- **类型**: integer
- **默认值**: 200000
- **说明**: 最大上下文 token 数量

#### agent.enable_context_pruning
- **类型**: boolean
- **默认值**: true
- **说明**: 启用上下文自动修剪

#### agent.context_pruning_threshold
- **类型**: float
- **默认值**: 0.8
- **范围**: 0.0-1.0
- **说明**: 上下文修剪阈值 (当使用率超过此值时触发修剪)

#### agent.enable_turn_validation
- **类型**: boolean
- **默认值**: true
- **说明**: 启用回合验证 (检测上下文污染)

### 2. Providers 配置

#### providers.primary
- **类型**: string
- **必填**: 是
- **说明**: 主 LLM 提供商名称

#### providers.fallbacks
- **类型**: array[string]
- **默认值**: []
- **说明**: 备用提供商列表 (按优先级排序)

#### providers.retry_max_attempts
- **类型**: integer
- **默认值**: 3
- **范围**: 1-10
- **说明**: 请求失败时的最大重试次数

#### providers.retry_delay_ms
- **类型**: integer
- **默认值**: 1000
- **说明**: 重试延迟 (毫秒)

#### providers.cooldown_duration_ms
- **类型**: integer
- **默认值**: 60000
- **说明**: 提供商冷却时间 (毫秒)

#### providers.<provider_name>
每个提供商的具体配置:

```json5
"anthropic": {
  "api_key": "${ANTHROPIC_API_KEY}",  // API 密钥 (支持环境变量)
  "base_url": "https://api.anthropic.com",  // API 基础 URL
  "timeout_ms": 120000,  // 请求超时 (毫秒)
  "max_retries": 3       // 最大重试次数
}
```

### 3. Gateway 配置

#### gateway.port
- **类型**: integer
- **默认值**: 8765
- **范围**: 1024-65535
- **说明**: WebSocket 服务器端口

#### gateway.host
- **类型**: string
- **默认值**: "0.0.0.0"
- **说明**: 监听地址 ("0.0.0.0" 表示所有接口)

#### gateway.auth.mode
- **类型**: string
- **默认值**: "none"
- **可选值**: "none", "token"
- **说明**: 认证模式

#### gateway.auth.token
- **类型**: string
- **说明**: 认证 token (当 mode 为 "token" 时必填)

#### gateway.max_connections
- **类型**: integer
- **默认值**: 100
- **说明**: 最大并发连接数

#### gateway.message_size_limit
- **类型**: integer
- **默认值**: 10485760 (10 MB)
- **说明**: 单个消息的最大大小 (字节)

### 4. Channels 配置

#### channels.telegram
Telegram Bot 配置:

```json5
"telegram": {
  "enabled": true,                    // 启用 Telegram 通道
  "bot_token": "${TELEGRAM_BOT_TOKEN}",  // Bot Token
  "allowed_users": [],                // 允许的用户 ID 列表 (空表示所有用户)
  "dedup_window_seconds": 300,        // 去重窗口 (秒)
  "thread_binding_enabled": true      // 启用线程绑定
}
```

#### channels.slack
Slack Bot 配置:

```json5
"slack": {
  "enabled": false,
  "bot_token": "${SLACK_BOT_TOKEN}",
  "app_token": "${SLACK_APP_TOKEN}"
}
```

### 5. System 配置

#### system.workspace_dir
- **类型**: string
- **默认值**: "~/.ravbot/workspace"
- **说明**: Agent 工作空间目录

#### system.sessions_dir
- **类型**: string
- **默认值**: "~/.ravbot/sessions"
- **说明**: 会话存储目录

#### system.plugins_dir
- **类型**: string
- **默认值**: "~/.ravbot/plugins"
- **说明**: 插件目录

#### system.log_level
- **类型**: string
- **默认值**: "info"
- **可选值**: "trace", "debug", "info", "warn", "error", "critical"
- **说明**: 日志级别

#### system.log_file
- **类型**: string
- **默认值**: "~/.ravbot/logs/ravbot.log"
- **说明**: 日志文件路径

### 6. Security 配置

#### security.sandbox.enabled
- **类型**: boolean
- **默认值**: true
- **说明**: 启用沙箱模式

#### security.sandbox.allowed_commands
- **类型**: array[string]
- **说明**: 允许执行的命令列表

#### security.sandbox.blocked_paths
- **类型**: array[string]
- **说明**: 禁止访问的路径列表

#### security.rbac.enabled
- **类型**: boolean
- **默认值**: true
- **说明**: 启用基于角色的访问控制

#### security.rate_limiter.enabled
- **类型**: boolean
- **默认值**: true
- **说明**: 启用速率限制

#### security.rate_limiter.requests_per_minute
- **类型**: integer
- **默认值**: 60
- **说明**: 每分钟最大请求数

#### security.audit.enabled
- **类型**: boolean
- **默认值**: true
- **说明**: 启用安全审计日志

### 7. MCP 配置

MCP (Model Context Protocol) 服务器配置:

```json5
"mcp": {
  "servers": {
    "server_name": {
      "command": "command_to_run",
      "args": ["arg1", "arg2"],
      "env": {
        "ENV_VAR": "value"
      }
    }
  }
}
```

### 8. Cron 配置

定时任务配置:

```json5
"cron": {
  "enabled": true,
  "jobs": [
    {
      "name": "job_name",
      "schedule": "0 2 * * *",  // Cron 表达式
      "command": "command_name",
      "params": {}
    }
  ]
}
```

## 环境变量

配置文件支持环境变量替换,使用 `${VAR_NAME}` 语法:

```json5
{
  "providers": {
    "anthropic": {
      "api_key": "${ANTHROPIC_API_KEY}"
    }
  }
}
```

### 常用环境变量

- `ANTHROPIC_API_KEY`: Anthropic API 密钥
- `OPENAI_API_KEY`: OpenAI API 密钥
- `GATEWAY_TOKEN`: Gateway 认证 token
- `TELEGRAM_BOT_TOKEN`: Telegram Bot token
- `SLACK_BOT_TOKEN`: Slack Bot token

## 配置验证

使用 CLI 验证配置文件:

```bash
ravbot config validate config.json
```

或在代码中验证:

```cpp
#include "ravbot/config.hpp"

auto config_json = /* 加载 JSON */;
auto errors = RavBotConfig::Validate(config_json);

if (!errors.empty()) {
    for (const auto& error : errors) {
        std::cerr << "Config error: " << error << std::endl;
    }
}
```

## 配置合并

RavBot 支持配置合并,按以下优先级:

1. 命令行参数
2. 环境变量
3. 项目配置文件 (`./config.json`)
4. 用户配置文件 (`~/.ravbot/config.json`)
5. 默认配置

使用 `Merge()` 方法合并配置:

```cpp
auto base_config = RavBotConfig::LoadFromFile("base.json");
auto override_config = RavBotConfig::LoadFromFile("override.json");
auto merged = RavBotConfig::Merge(base_config.ToJson(), override_config.ToJson());
```

## 最佳实践

### 1. 使用环境变量存储敏感信息

```json5
{
  "providers": {
    "anthropic": {
      "api_key": "${ANTHROPIC_API_KEY}"  // 不要硬编码 API 密钥
    }
  }
}
```

### 2. 分离开发和生产配置

```bash
# 开发环境
config.dev.json

# 生产环境
config.prod.json
```

### 3. 启用安全特性

```json5
{
  "security": {
    "sandbox": {"enabled": true},
    "rbac": {"enabled": true},
    "rate_limiter": {"enabled": true},
    "audit": {"enabled": true}
  }
}
```

### 4. 配置故障转移

```json5
{
  "providers": {
    "primary": "anthropic",
    "fallbacks": ["openai"],  // 配置备用提供商
    "retry_max_attempts": 3
  }
}
```

### 5. 优化性能

```json5
{
  "agent": {
    "enable_context_pruning": true,  // 启用上下文修剪
    "context_pruning_threshold": 0.8
  },
  "gateway": {
    "max_connections": 100  // 根据负载调整
  }
}
```

## 故障排查

### 配置文件未找到

```
Error: Config file not found: config.json
```

**解决方案**: 确保配置文件存在于正确的位置,或使用 `--config` 参数指定路径。

### 环境变量未设置

```
Error: Environment variable not set: ANTHROPIC_API_KEY
```

**解决方案**: 设置所需的环境变量:

```bash
export ANTHROPIC_API_KEY="your-api-key"
```

### 配置验证失败

```
Config error: Missing required field: providers.primary
```

**解决方案**: 检查配置文件是否包含所有必填字段。

## 参考链接

- [API Reference](API_REFERENCE.md)
- [配置示例](../config.example.json)
- [环境变量列表](../README.md#environment-variables)

## 版本信息

- **配置版本**: 1.0
- **最后更新**: 2026-03-14
