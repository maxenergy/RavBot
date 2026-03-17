# RavBot Ubuntu 24.04 DEB 包

## 快速安装

### 方法 1: 从源码构建并安装

```bash
# 1. 克隆仓库
git clone https://github.com/RavBot/RavBot.git
cd RavBot

# 2. 安装构建依赖
sudo apt-get update
sudo apt-get install -y \
    dpkg-dev debhelper devscripts \
    build-essential cmake \
    libssl-dev libcurl4-openssl-dev \
    nlohmann-json3-dev libspdlog-dev \
    zlib1g-dev libsqlite3-dev \
    nodejs npm

# 3. 构建 DEB 包
./scripts/build-deb.sh

# 4. 安装
sudo dpkg -i dist/ravbot_0.3.0-1_amd64.deb
sudo apt-get install -f  # 如果有依赖问题
```

### 方法 2: 本地构建(无需 sudo)

```bash
# 检查并安装依赖后
./scripts/build-deb-local.sh

# 安装生成的包
sudo dpkg -i dist/ravbot_0.3.0-1_amd64.deb
```

## 初始化配置

```bash
# 1. 运行初始化向导(在当前用户下)
ravbot onboard

# 2. 启动服务(前台运行)
ravbot gateway

# 或者安装为系统服务
ravbot gateway install
systemctl start ravbot
systemctl enable ravbot

# 3. 查看状态
ravbot health

# 4. 打开 Dashboard
ravbot dashboard
```

## 包内容

### 安装位置

- **主程序**: `/usr/bin/ravbot`
- **技能库**: `/usr/share/ravbot/skills/`
- **Sidecar**: `/usr/share/ravbot/sidecar/` (可选,需要 Node.js)
- **服务文件**: `/lib/systemd/system/ravbot.service`
- **文档**: `/usr/share/doc/ravbot/`

### 运行时文件(在当前用户主目录)

- **配置**: `~/.ravbot/ravbot.json`
- **工作区**: `~/.ravbot/agents/main/workspace/`
- **会话**: `~/.ravbot/agents/main/sessions/`
- **日志**: `~/.ravbot/logs/`
- **插件**: `~/.ravbot/plugins/` (可选)

## 配置 API 密钥

```bash
# OpenAI
ravbot config set providers.openai.apiKey "sk-..."

# Anthropic
ravbot config set providers.anthropic.apiKey "sk-ant-..."

# 重新加载配置
ravbot config reload
```

## 关于 Node.js Sidecar

**Node.js Sidecar 已包含在 DEB 包中,用于 OpenClaw 插件兼容性。**

### Sidecar 提供的功能

- ✅ 运行 OpenClaw TypeScript 插件
- ✅ 插件工具 (Tools)
- ✅ 插件钩子 (Hooks)
- ✅ 插件服务 (Services)
- ✅ 自定义 LLM Provider
- ✅ HTTP 路由扩展

### 使用插件

```bash
# 1. 安装插件到用户目录
mkdir -p ~/.ravbot/plugins/my-plugin
cd ~/.ravbot/plugins/my-plugin

# 2. 创建插件配置
cat > ravbot.plugin.json << 'EOF'
{
  "name": "my-plugin",
  "version": "1.0.0",
  "main": "index.js"
}
EOF

# 3. 启用插件
ravbot config set plugins.allow '["my-plugin"]'

# 4. 重启 gateway
ravbot gateway restart
```

### 验证 Sidecar

```bash
# 检查 Sidecar 是否正确安装
ls -la /usr/share/ravbot/sidecar/dist/
ls -la /usr/share/ravbot/sidecar/node_modules/

# 查看插件状态
ravbot plugins list
```

## 常用命令

```bash
# 查看版本
ravbot --version

# 健康检查
ravbot health

# 查看状态
ravbot status

# 发送消息
ravbot agent "Hello!"

# 查看会话
ravbot sessions list

# 查看日志
ravbot logs
```

## 卸载

```bash
# 保留配置
sudo apt-get remove ravbot

# 完全删除(包括配置)
sudo apt-get purge ravbot
rm -rf ~/.ravbot  # 手动删除用户数据
```

## 故障排查

### 服务无法启动

```bash
# 查看详细日志
journalctl -u ravbot -n 50 --no-pager

# 手动运行(调试)
ravbot gateway

# 检查配置
ravbot config get
```

### 端口冲突

```bash
# 检查端口占用
sudo netstat -tlnp | grep -E '18800|18801'

# 修改端口
ravbot config set gateway.port 18900
ravbot config set gateway.controlUi.port 18901
```

## 系统要求

- **操作系统**: Ubuntu 24.04 LTS (Noble Numbat)
- **架构**: amd64 (x86_64) 或 arm64 (aarch64)
- **内存**: 最低 512MB,推荐 1GB+
- **磁盘**: 最低 500MB,推荐 2GB+
- **必需依赖**: libssl3, libcurl4, libsqlite3-0, zlib1g, nodejs (>= 18)
- **构建依赖**: 上述依赖 + npm, cmake, g++, 开发库

## 功能特性

### 核心功能
- ✅ 高性能 C++17 实现
- ✅ OpenClaw 完全兼容
- ✅ 多 LLM Provider (OpenAI, Anthropic, Google, Qwen)
- ✅ WebSocket RPC + HTTP REST API
- ✅ 会话持久化
- ✅ 技能系统
- ✅ MCP 协议支持
- ✅ 安全沙箱
- ✅ 886 个测试通过

### 插件系统 (Node.js Sidecar)
- 🔌 OpenClaw TypeScript 插件生态系统
- 🔌 自定义工具和 Hook
- 🔌 插件服务和 Provider
- 🔌 HTTP 路由扩展
- 🔌 完整的 OpenClaw 插件兼容性

## 更多信息

- **官方文档**: https://ravbot.github.io
- **GitHub**: https://github.com/RavBot/RavBot
- **问题反馈**: https://github.com/RavBot/RavBot/issues
- **详细打包指南**: [docs/DEB_PACKAGING.md](docs/DEB_PACKAGING.md)

## 许可证

Apache License 2.0
