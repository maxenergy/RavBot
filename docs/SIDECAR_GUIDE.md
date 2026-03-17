# RavBot Sidecar 说明

## 什么是 Sidecar？

Sidecar 是一个 **Node.js 辅助进程**，用于运行 OpenClaw 兼容的 TypeScript 插件。

```
┌─────────────────────────────────────────────────────────────┐
│                    RavBot 架构                            │
└─────────────────────────────────────────────────────────────┘

┌──────────────────────┐         TCP Socket         ┌─────────────────────┐
│   C++ 主进程          │ ◄────────────────────────► │  Node.js Sidecar    │
│   (ravbot)        │    JSON-RPC 2.0 通信        │  (插件运行时)        │
├──────────────────────┤                            ├─────────────────────┤
│ ✅ Agent Loop        │                            │ 🔌 加载 TS 插件      │
│ ✅ LLM Provider      │                            │ 🔌 执行插件工具      │
│ ✅ WebSocket RPC     │                            │ 🔌 调用插件 Hook    │
│ ✅ HTTP API          │                            │ 🔌 运行插件服务      │
│ ✅ Session 管理      │                            │ 🔌 自定义 Provider  │
│ ✅ 所有核心功能      │                            │ 🔌 HTTP 路由扩展    │
└──────────────────────┘                            └─────────────────────┘
```

## 为什么需要 Sidecar？

**问题：** RavBot 是 C++ 实现，但 OpenClaw 的插件生态是 TypeScript

**解决方案：** 使用 Node.js 进程运行 TypeScript 插件，通过 IPC 与 C++ 通信

## DEB 包中的 Sidecar

### 安装位置

```
/usr/share/ravbot/sidecar/
├── dist/                    # 编译后的 JavaScript
│   ├── index.js            # 入口文件
│   ├── plugin-loader.js    # 插件加载器
│   ├── rpc-server.js       # RPC 服务器
│   └── ...
├── node_modules/           # Node.js 依赖
│   ├── jiti/              # TypeScript 运行时
│   └── ...
├── package.json            # 包配置
└── package-lock.json       # 依赖锁定
```

### 构建过程

DEB 包构建时会自动：

1. **安装依赖** - `npm ci --production`
2. **编译 TypeScript** - `npm run build`
3. **打包到 DEB** - 包含 `dist/` 和 `node_modules/`

### 运行时

当 RavBot 启动时：

```bash
# C++ 主进程启动
ravbot gateway
  ↓
# 检测到插件配置
plugins.allow = ["my-plugin"]
  ↓
# 启动 Sidecar 进程
node /usr/share/ravbot/sidecar/dist/index.js
  ↓
# 通过环境变量传递配置
RAVBOT_PORT=18802
RAVBOT_PLUGIN_CONFIG='{"allow":["my-plugin"]}'
  ↓
# Sidecar 连接到 C++ 进程
TCP Socket: 127.0.0.1:18802
  ↓
# 加载插件
~/.ravbot/plugins/my-plugin/
  ↓
# 双向 JSON-RPC 通信
C++ ←→ Sidecar ←→ Plugin
```

## 插件系统

### 支持的插件功能

1. **Tools** - 自定义工具
   ```typescript
   export const tools = [{
     name: "my_tool",
     description: "My custom tool",
     input_schema: { type: "object", properties: {} },
     execute: async (input) => { return "result"; }
   }];
   ```

2. **Hooks** - 生命周期钩子
   ```typescript
   export const hooks = {
     "agent.before_turn": async (context) => {
       console.log("Before turn");
     }
   };
   ```

3. **Services** - 后台服务
   ```typescript
   export const services = [{
     name: "my_service",
     start: async () => { /* 启动服务 */ },
     stop: async () => { /* 停止服务 */ }
   }];
   ```

4. **Providers** - 自定义 LLM Provider
   ```typescript
   export const providers = [{
     name: "my_provider",
     chat: async (messages) => { /* 调用 LLM */ }
   }];
   ```

5. **Commands** - Slash 命令
   ```typescript
   export const commands = [{
     name: "mycommand",
     execute: async (args) => { /* 执行命令 */ }
   }];
   ```

6. **HTTP Routes** - HTTP 路由
   ```typescript
   export const routes = [{
     method: "GET",
     path: "/plugins/my-plugin/status",
     handler: async (req, res) => { res.json({ok: true}); }
   }];
   ```

### 插件目录结构

```
~/.ravbot/plugins/
├── my-plugin/
│   ├── ravbot.plugin.json    # 插件配置
│   ├── index.js                 # 入口文件
│   ├── package.json             # 依赖
│   └── node_modules/            # 插件依赖
└── another-plugin/
    └── ...
```

### 插件配置示例

```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "description": "My custom plugin",
  "main": "index.js",
  "capabilities": {
    "tools": true,
    "hooks": true,
    "services": false
  }
}
```

## 使用插件

### 1. 安装插件

```bash
# 创建插件目录
mkdir -p ~/.ravbot/plugins/weather-plugin
cd ~/.ravbot/plugins/weather-plugin

# 创建插件文件
cat > ravbot.plugin.json << 'EOF'
{
  "name": "weather-plugin",
  "version": "1.0.0",
  "main": "index.js"
}
EOF

cat > index.js << 'EOF'
export const tools = [{
  name: "get_weather",
  description: "Get current weather",
  input_schema: {
    type: "object",
    properties: {
      city: { type: "string" }
    }
  },
  execute: async (input) => {
    return `Weather in ${input.city}: Sunny, 25°C`;
  }
}];
EOF
```

### 2. 启用插件

```bash
# 方法 1: 通过配置文件
ravbot config set plugins.allow '["weather-plugin"]'

# 方法 2: 编辑配置文件
nano ~/.ravbot/ravbot.json
```

```json
{
  "plugins": {
    "allow": ["weather-plugin"],
    "deny": []
  }
}
```

### 3. 重启 Gateway

```bash
ravbot gateway restart
```

### 4. 验证插件

```bash
# 查看已加载的插件
ravbot plugins list

# 查看插件工具
curl http://localhost:18801/api/plugins/tools

# 调用插件工具
ravbot agent "What's the weather in Beijing?"
```

## 调试 Sidecar

### 查看 Sidecar 日志

```bash
# 启用详细日志
export RAVBOT_VERBOSE=1
ravbot gateway

# 或查看系统日志
journalctl -u ravbot -f | grep sidecar
```

### 手动测试 Sidecar

```bash
# 设置环境变量
export RAVBOT_PORT=18802
export RAVBOT_PLUGIN_CONFIG='{"allow":[]}'

# 手动运行 Sidecar
node /usr/share/ravbot/sidecar/dist/index.js
```

### 常见问题

**1. Sidecar 无法启动**
```bash
# 检查 Node.js 版本
node --version  # 应该 >= 18

# 检查 Sidecar 文件
ls -la /usr/share/ravbot/sidecar/dist/
ls -la /usr/share/ravbot/sidecar/node_modules/
```

**2. 插件加载失败**
```bash
# 检查插件配置
cat ~/.ravbot/ravbot.json | grep -A 5 plugins

# 检查插件目录
ls -la ~/.ravbot/plugins/

# 查看详细错误
RAVBOT_VERBOSE=1 ravbot gateway
```

**3. 插件工具不可用**
```bash
# 验证插件已加载
ravbot plugins list

# 查看工具列表
curl http://localhost:18801/api/plugins/tools
```

## 性能考虑

### Sidecar 开销

- **内存**: ~50-100MB (Node.js 运行时 + 插件)
- **启动时间**: ~100-200ms
- **IPC 延迟**: <1ms (TCP loopback)

### 优化建议

1. **按需加载** - 只启用需要的插件
2. **缓存结果** - 在插件中缓存重复计算
3. **异步执行** - 使用 async/await 避免阻塞
4. **资源限制** - 在插件中设置超时和内存限制

## 安全考虑

### Sidecar 隔离

- ✅ 独立进程 (与 C++ 主进程隔离)
- ✅ TCP loopback (仅本地通信)
- ✅ JSON-RPC 协议 (结构化通信)
- ⚠️ 插件代码在 Sidecar 进程中运行 (需要信任插件)

### 插件安全

**建议：**
- 只安装信任的插件
- 审查插件代码
- 使用 `plugins.allow` 白名单
- 定期更新插件

**不建议：**
- 运行未知来源的插件
- 给插件过高权限
- 在生产环境运行未测试的插件

## 开发插件

### 快速开始

```bash
# 1. 创建插件目录
mkdir -p ~/.ravbot/plugins/my-plugin
cd ~/.ravbot/plugins/my-plugin

# 2. 初始化 npm 项目
npm init -y

# 3. 安装 TypeScript (可选)
npm install -D typescript @types/node

# 4. 创建插件
cat > index.ts << 'EOF'
export const tools = [{
  name: "hello",
  description: "Say hello",
  input_schema: {
    type: "object",
    properties: {
      name: { type: "string" }
    }
  },
  execute: async (input: any) => {
    return `Hello, ${input.name}!`;
  }
}];
EOF

# 5. 编译 (如果使用 TypeScript)
npx tsc index.ts

# 6. 创建插件配置
cat > ravbot.plugin.json << 'EOF'
{
  "name": "my-plugin",
  "version": "1.0.0",
  "main": "index.js"
}
EOF
```

### 插件 API 参考

查看 OpenClaw 插件文档:
- https://github.com/openclaw/openclaw/tree/main/docs/plugins

## 总结

- ✅ Sidecar 已包含在 DEB 包中
- ✅ 自动编译和打包
- ✅ 完整的 OpenClaw 插件兼容性
- ✅ 支持所有插件功能 (Tools, Hooks, Services, etc.)
- ✅ 安全的进程隔离
- ✅ 高性能 IPC 通信

**开始使用：**
```bash
# 安装 DEB 包
sudo dpkg -i ravbot_*.deb

# 启用插件
ravbot config set plugins.allow '["my-plugin"]'

# 重启 gateway
ravbot gateway restart
```
