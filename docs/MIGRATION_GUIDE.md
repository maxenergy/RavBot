# QuantClaw Migration Guide

## 概述

本指南帮助您从旧版本的 QuantClaw 迁移到最新版本 (v0.3.0)。

## 版本兼容性

| 源版本 | 目标版本 | 迁移难度 | 破坏性变更 |
|--------|----------|----------|------------|
| v0.1.x | v0.3.0   | 中等     | 是         |
| v0.2.x | v0.3.0   | 简单     | 少量       |

## 从 v0.2.x 迁移到 v0.3.0

### 1. 配置文件变更

#### 1.1 新增配置项

v0.3.0 引入了以下新配置项:

```json5
{
  "agent": {
    // 新增: 上下文修剪配置
    "enable_context_pruning": true,
    "context_pruning_threshold": 0.8,

    // 新增: 回合验证
    "enable_turn_validation": true
  },

  "providers": {
    // 新增: 故障转移配置
    "fallbacks": ["openai"],
    "cooldown_duration_ms": 60000
  },

  "security": {
    // 新增: 安全审计
    "audit": {
      "enabled": true,
      "log_dir": "~/.quantclaw/logs/audit"
    }
  }
}
```

**迁移步骤**:

1. 备份现有配置文件:
   ```bash
   cp ~/.quantclaw/config.json ~/.quantclaw/config.json.backup
   ```

2. 添加新配置项到您的配置文件

3. 验证配置:
   ```bash
   quantclaw config validate ~/.quantclaw/config.json
   ```

#### 1.2 配置项重命名

| 旧名称 | 新名称 | 说明 |
|--------|--------|------|
| `agent.max_turns` | `agent.max_iterations` | 更准确的命名 |
| `gateway.token` | `gateway.auth.token` | 嵌套结构 |

**迁移脚本**:

```bash
#!/bin/bash
# migrate_config.sh

CONFIG_FILE="$HOME/.quantclaw/config.json"

# 备份
cp "$CONFIG_FILE" "$CONFIG_FILE.backup"

# 使用 jq 更新配置
jq '.agent.max_iterations = .agent.max_turns | del(.agent.max_turns)' "$CONFIG_FILE" > "$CONFIG_FILE.tmp"
jq '.gateway.auth.token = .gateway.token | del(.gateway.token)' "$CONFIG_FILE.tmp" > "$CONFIG_FILE"
rm "$CONFIG_FILE.tmp"

echo "Configuration migrated successfully"
```

### 2. API 变更

#### 2.1 AgentLoop 接口变更

**旧版本 (v0.2.x)**:
```cpp
AgentResponse ProcessRequest(const std::string& message,
                            const std::string& session_id);
```

**新版本 (v0.3.0)**:
```cpp
AgentResponse ProcessRequest(const AgentRequest& request);
```

**迁移示例**:

```cpp
// 旧代码
auto response = agent_loop.ProcessRequest("Hello", "session-123");

// 新代码
AgentRequest request;
request.message = "Hello";
request.session_id = "session-123";
auto response = agent_loop.ProcessRequest(request);
```

#### 2.2 ProviderRegistry 新增方法

v0.3.0 新增了故障转移相关方法:

```cpp
// 新增方法
void AddFallbackProvider(const std::string& name);
FailoverStats GetFailoverStats() const;
void ResetCooldowns();
```

**使用示例**:

```cpp
auto registry = std::make_shared<ProviderRegistry>(logger);
registry->SetPrimaryProvider("anthropic");
registry->AddFallbackProvider("openai");  // 新增

// 获取故障转移统计
auto stats = registry->GetFailoverStats();  // 新增
std::cout << "Failover count: " << stats.failover_count << std::endl;
```

#### 2.3 SecurityAuditLogger 新增

v0.3.0 引入了安全审计日志系统:

```cpp
#include "quantclaw/security/audit_logger.hpp"

auto audit_logger = std::make_shared<SecurityAuditLogger>(log_dir, logger);

// 记录安全事件
audit_logger->LogContentWrapping("web_search", "content preview");
audit_logger->LogDangerousToolCall("bash", params);
audit_logger->LogAuthFailure("client-123", "invalid token");
```

### 3. 数据库迁移

#### 3.1 Session 格式变更

v0.3.0 的 session 文件格式有所变化:

**旧格式**:
```json
{
  "session_id": "abc",
  "messages": [...]
}
```

**新格式**:
```json
{
  "session_id": "abc",
  "messages": [...],
  "metadata": {
    "created_at": "2026-03-14T10:00:00Z",
    "last_accessed": "2026-03-14T10:30:00Z"
  }
}
```

**迁移脚本**:

```python
#!/usr/bin/env python3
# migrate_sessions.py

import json
import os
from pathlib import Path
from datetime import datetime

def migrate_session(session_file):
    with open(session_file, 'r') as f:
        data = json.load(f)

    # 添加 metadata
    if 'metadata' not in data:
        data['metadata'] = {
            'created_at': datetime.now().isoformat() + 'Z',
            'last_accessed': datetime.now().isoformat() + 'Z'
        }

    with open(session_file, 'w') as f:
        json.dump(data, f, indent=2)

def main():
    sessions_dir = Path.home() / '.quantclaw' / 'sessions'
    for session_file in sessions_dir.glob('*.json'):
        print(f"Migrating {session_file}")
        migrate_session(session_file)
    print("Migration complete")

if __name__ == '__main__':
    main()
```

运行迁移:
```bash
python3 migrate_sessions.py
```

### 4. 插件系统变更

#### 4.1 Plugin Manifest 格式

v0.3.0 的插件清单格式更加严格:

**必填字段**:
- `id`: 插件 ID (字母数字、连字符、下划线)
- `name`: 插件名称
- `version`: 版本号 (semver 格式)
- `entryPoint`: 入口文件

**新增字段**:
- `schemaVersion`: 清单格式版本 (默认 "1.0")
- `dependencies`: 插件依赖列表

**迁移示例**:

```json5
// 旧格式
{
  "id": "my-plugin",
  "name": "My Plugin"
}

// 新格式
{
  "id": "my-plugin",
  "name": "My Plugin",
  "version": "1.0.0",        // 必填
  "entryPoint": "index.js",  // 必填
  "schemaVersion": "1.0"     // 推荐
}
```

### 5. 依赖更新

#### 5.1 编译依赖

v0.3.0 更新了以下依赖:

| 依赖 | 旧版本 | 新版本 |
|------|--------|--------|
| spdlog | 1.11.x | 1.12.x |
| nlohmann_json | 3.11.x | 3.11.x |
| IXWebSocket | 11.4.4 | 11.4.5 |

**更新步骤**:

```bash
# 清理旧的构建
rm -rf build

# 重新配置和构建
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 从 v0.1.x 迁移到 v0.3.0

### 1. 重大架构变更

v0.2.x 和 v0.3.0 引入了多项重大架构变更:

1. **Gateway 系统**: 新增 WebSocket RPC 接口
2. **Provider Registry**: 统一的 LLM 提供商管理
3. **Plugin System**: 动态插件加载
4. **Security Module**: 完整的安全框架

### 2. 迁移策略

由于变更较大,建议采用以下策略:

#### 2.1 渐进式迁移

1. **阶段 1**: 保持 v0.1.x 运行,并行部署 v0.3.0
2. **阶段 2**: 逐步迁移功能到 v0.3.0
3. **阶段 3**: 完全切换到 v0.3.0

#### 2.2 数据迁移

```bash
# 1. 导出 v0.1.x 数据
quantclaw-v0.1 export --output data.json

# 2. 转换数据格式
python3 convert_data.py data.json data_v0.3.json

# 3. 导入到 v0.3.0
quantclaw import --input data_v0.3.json
```

#### 2.3 代码重写

v0.1.x 的代码需要大量重写。参考 [API Reference](API_REFERENCE.md) 了解新 API。

## 破坏性变更清单

### v0.3.0 破坏性变更

1. **配置文件格式**: 部分配置项重命名和重组
2. **API 接口**: `AgentLoop::ProcessRequest()` 参数变更
3. **Session 格式**: 新增 metadata 字段
4. **Plugin Manifest**: 更严格的验证规则

### v0.2.0 破坏性变更

1. **Gateway 引入**: 需要配置 gateway 端口和认证
2. **Provider 抽象**: 统一的 LLMProvider 接口
3. **配置结构**: 嵌套的配置结构

## 回滚指南

如果迁移遇到问题,可以回滚到旧版本:

### 1. 恢复配置文件

```bash
cp ~/.quantclaw/config.json.backup ~/.quantclaw/config.json
```

### 2. 恢复 session 数据

```bash
cp -r ~/.quantclaw/sessions.backup ~/.quantclaw/sessions
```

### 3. 重新安装旧版本

```bash
# 卸载新版本
sudo rm /usr/local/bin/quantclaw

# 安装旧版本
cd quantclaw-v0.2.x
cmake --build build --target install
```

## 测试迁移

迁移后,运行以下测试确保系统正常:

### 1. 配置验证

```bash
quantclaw config validate
```

### 2. 连接测试

```bash
quantclaw gateway test
```

### 3. Agent 测试

```bash
quantclaw agent test --message "Hello"
```

### 4. 运行测试套件

```bash
cd build
ctest --output-on-failure
```

## 常见问题

### Q1: 迁移后无法连接 Gateway

**问题**: `Connection refused: localhost:8765`

**解决方案**:
1. 检查 Gateway 是否启动: `quantclaw gateway status`
2. 检查端口配置: `cat ~/.quantclaw/config.json | grep port`
3. 检查防火墙设置

### Q2: Session 数据丢失

**问题**: 旧的 session 无法加载

**解决方案**:
1. 运行 session 迁移脚本: `python3 migrate_sessions.py`
2. 检查 session 文件格式
3. 查看日志: `tail -f ~/.quantclaw/logs/quantclaw.log`

### Q3: Plugin 加载失败

**问题**: `Plugin manifest validation failed`

**解决方案**:
1. 更新 plugin manifest 格式
2. 添加必填字段: `version`, `entryPoint`
3. 验证 manifest: `quantclaw plugin validate plugin.json`

### Q4: API 调用失败

**问题**: `Method signature mismatch`

**解决方案**:
1. 参考 [API Reference](API_REFERENCE.md) 更新代码
2. 使用新的 `AgentRequest` 结构
3. 重新编译应用

## 获取帮助

如果遇到迁移问题:

1. **查看文档**: [API Reference](API_REFERENCE.md), [Configuration Guide](CONFIGURATION_GUIDE.md)
2. **查看日志**: `~/.quantclaw/logs/quantclaw.log`
3. **运行诊断**: `quantclaw diagnose`
4. **提交 Issue**: [GitHub Issues](https://github.com/yourusername/quantclaw/issues)

## 迁移检查清单

- [ ] 备份配置文件和数据
- [ ] 更新配置文件格式
- [ ] 迁移 session 数据
- [ ] 更新插件 manifest
- [ ] 更新应用代码
- [ ] 运行配置验证
- [ ] 运行测试套件
- [ ] 测试核心功能
- [ ] 监控日志和性能
- [ ] 更新文档和脚本

## 版本信息

- **当前版本**: v0.3.0
- **最后更新**: 2026-03-14
- **下一个版本**: v0.4.0 (计划中)

## 参考链接

- [Release Notes](RELEASE_NOTES_v0.3.0.md)
- [API Reference](API_REFERENCE.md)
- [Configuration Guide](CONFIGURATION_GUIDE.md)
- [GitHub Repository](https://github.com/yourusername/quantclaw)
