# QuantClaw Ubuntu 24.04 DEB 包

## 快速安装

### 方法 1: 从源码构建并安装

```bash
# 1. 克隆仓库
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw

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
sudo dpkg -i dist/quantclaw_0.3.0-1_amd64.deb
sudo apt-get install -f  # 如果有依赖问题
```

### 方法 2: 本地构建(无需 sudo)

```bash
# 检查并安装依赖后
./scripts/build-deb-local.sh

# 安装生成的包
sudo dpkg -i dist/quantclaw_0.3.0-1_amd64.deb
```

## 初始化配置

```bash
# 1. 运行初始化向导(在当前用户下)
quantclaw onboard

# 2. 启动服务(前台运行)
quantclaw gateway

# 或者安装为系统服务
quantclaw gateway install
systemctl start quantclaw
systemctl enable quantclaw

# 3. 查看状态
quantclaw health

# 4. 打开 Dashboard
quantclaw dashboard
```

## 包内容

### 安装位置

- **主程序**: `/usr/bin/quantclaw`
- **技能库**: `/usr/share/quantclaw/skills/`
- **Sidecar**: `/usr/share/quantclaw/sidecar/` (可选,需要 Node.js)
- **服务文件**: `/lib/systemd/system/quantclaw.service`
- **文档**: `/usr/share/doc/quantclaw/`

### 运行时文件(在当前用户主目录)

- **配置**: `~/.quantclaw/quantclaw.json`
- **工作区**: `~/.quantclaw/agents/main/workspace/`
- **会话**: `~/.quantclaw/agents/main/sessions/`
- **日志**: `~/.quantclaw/logs/`
- **插件**: `~/.quantclaw/plugins/` (可选)

## 配置 API 密钥

```bash
# OpenAI
quantclaw config set providers.openai.apiKey "sk-..."

# Anthropic
quantclaw config set providers.anthropic.apiKey "sk-ant-..."

# 重新加载配置
quantclaw config reload
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
mkdir -p ~/.quantclaw/plugins/my-plugin
cd ~/.quantclaw/plugins/my-plugin

# 2. 创建插件配置
cat > quantclaw.plugin.json << 'EOF'
{
  "name": "my-plugin",
  "version": "1.0.0",
  "main": "index.js"
}
EOF

# 3. 启用插件
quantclaw config set plugins.allow '["my-plugin"]'

# 4. 重启 gateway
quantclaw gateway restart
```

### 验证 Sidecar

```bash
# 检查 Sidecar 是否正确安装
ls -la /usr/share/quantclaw/sidecar/dist/
ls -la /usr/share/quantclaw/sidecar/node_modules/

# 查看插件状态
quantclaw plugins list
```

## 常用命令

```bash
# 查看版本
quantclaw --version

# 健康检查
quantclaw health

# 查看状态
quantclaw status

# 发送消息
quantclaw agent "Hello!"

# 查看会话
quantclaw sessions list

# 查看日志
quantclaw logs
```

## 卸载

```bash
# 保留配置
sudo apt-get remove quantclaw

# 完全删除(包括配置)
sudo apt-get purge quantclaw
rm -rf ~/.quantclaw  # 手动删除用户数据
```

## 故障排查

### 服务无法启动

```bash
# 查看详细日志
journalctl -u quantclaw -n 50 --no-pager

# 手动运行(调试)
quantclaw gateway

# 检查配置
quantclaw config get
```

### 端口冲突

```bash
# 检查端口占用
sudo netstat -tlnp | grep -E '18800|18801'

# 修改端口
quantclaw config set gateway.port 18900
quantclaw config set gateway.controlUi.port 18901
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

- **官方文档**: https://quantclaw.github.io
- **GitHub**: https://github.com/QuantClaw/QuantClaw
- **问题反馈**: https://github.com/QuantClaw/QuantClaw/issues
- **详细打包指南**: [docs/DEB_PACKAGING.md](docs/DEB_PACKAGING.md)

## 许可证

Apache License 2.0
