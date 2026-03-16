# DEB 打包设计更新说明

## 重要变更

根据用户反馈,DEB 打包设计已更新:

### 1. ✅ 用户数据位置修正

**之前(错误):**
- 创建专用系统用户 `quantclaw`
- 数据存储在 `/home/quantclaw/.quantclaw/`
- 需要 `sudo -u quantclaw` 运行命令

**现在(正确):**
- 不创建专用用户
- 数据存储在 `~/.quantclaw/` (当前用户主目录)
- 直接运行 `quantclaw` 命令,无需 sudo

### 2. ✅ Node.js 依赖说明

**Node.js 是可选的,仅用于 OpenClaw 插件支持**

#### 核心 C++ 功能(无需 Node.js):
- ✅ Agent Loop
- ✅ LLM Provider (OpenAI, Anthropic, Google, Qwen)
- ✅ WebSocket RPC + HTTP REST API
- ✅ Session 管理
- ✅ Skill 系统
- ✅ MCP 协议支持
- ✅ 内存管理和搜索
- ✅ 安全沙箱
- ✅ 所有内置工具

#### 可选功能(需要 Node.js):
- 🔌 OpenClaw TypeScript 插件生态系统
- 🔌 Plugin Sidecar (用于运行 TypeScript 插件)
- 🔌 自定义工具、Hook、服务

**依赖级别变更:**
- 从 `Recommends: nodejs` 改为 `Suggests: nodejs`
- 安装时不会自动安装 Node.js
- 仅在检测到 Node.js 时才安装 sidecar 依赖

## 使用场景

### 场景 1: 纯 C++ 使用(推荐)

```bash
# 安装
sudo dpkg -i quantclaw_*.deb

# 初始化
quantclaw onboard

# 运行
quantclaw gateway

# 配置
quantclaw config set providers.openai.apiKey "sk-..."
```

**无需 Node.js,所有核心功能可用!**

### 场景 2: 需要 OpenClaw 插件

```bash
# 1. 安装 QuantClaw
sudo dpkg -i quantclaw_*.deb

# 2. 安装 Node.js
sudo apt-get install nodejs npm

# 3. 安装 sidecar 依赖
cd /usr/share/quantclaw/sidecar
npm install --production

# 4. 启用插件
quantclaw config set plugins.allow '["my-plugin"]'
```

## 文件位置对比

### 系统文件(所有用户共享)
```
/usr/bin/quantclaw                    # 主程序
/usr/share/quantclaw/skills/          # 内置技能
/usr/share/quantclaw/sidecar/         # Sidecar(可选)
/lib/systemd/system/quantclaw.service # 服务模板
```

### 用户数据(每个用户独立)
```
~/.quantclaw/quantclaw.json           # 配置
~/.quantclaw/agents/main/workspace/   # 工作区
~/.quantclaw/agents/main/sessions/    # 会话
~/.quantclaw/logs/                    # 日志
~/.quantclaw/plugins/                 # 用户插件(可选)
```

## Systemd 服务

服务文件已更新为用户服务模式:

```bash
# 方法 1: 用户服务(推荐)
systemctl --user enable quantclaw
systemctl --user start quantclaw

# 方法 2: 系统服务(需要编辑服务文件指定用户)
sudo systemctl edit quantclaw
# 添加: User=your-username
sudo systemctl start quantclaw
```

## 安装后流程

### 旧流程(已废弃)
```bash
sudo dpkg -i quantclaw_*.deb
sudo -u quantclaw quantclaw onboard    # ❌ 错误
sudo systemctl start quantclaw         # ❌ 错误
```

### 新流程(正确)
```bash
# 1. 安装包
sudo dpkg -i quantclaw_*.deb

# 2. 初始化(当前用户)
quantclaw onboard

# 3. 运行
quantclaw gateway

# 或安装为服务
quantclaw gateway install
systemctl start quantclaw
```

## 卸载行为

### 包卸载
```bash
sudo apt-get remove quantclaw
```
- 删除 `/usr/bin/quantclaw`
- 删除 `/usr/share/quantclaw/`
- **保留** `~/.quantclaw/` (用户数据)

### 完全清理
```bash
sudo apt-get purge quantclaw
rm -rf ~/.quantclaw
```

## 迁移指南

如果你已经使用旧版本安装:

```bash
# 1. 停止旧服务
sudo systemctl stop quantclaw
sudo systemctl disable quantclaw

# 2. 备份数据(如果有)
sudo cp -r /home/quantclaw/.quantclaw ~/.quantclaw-backup

# 3. 卸载旧版本
sudo apt-get purge quantclaw

# 4. 安装新版本
sudo dpkg -i quantclaw_0.3.0-1_amd64.deb

# 5. 恢复数据(如果需要)
cp -r ~/.quantclaw-backup/* ~/.quantclaw/

# 6. 重新初始化
quantclaw onboard
```

## 技术细节

### 为什么有 Node.js Sidecar?

QuantClaw 是 C++ 实现,但为了兼容 OpenClaw 生态系统:
- OpenClaw 有丰富的 TypeScript 插件生态
- 这些插件使用 Node.js 运行时
- Sidecar 通过 TCP IPC 与 C++ 主进程通信
- 完全可选,不影响核心功能

### Sidecar 架构
```
┌─────────────────┐         TCP IPC          ┌──────────────────┐
│  C++ 主进程      │ ◄──────────────────────► │ Node.js Sidecar  │
│  (quantclaw)    │   JSON-RPC 2.0 (NDJSON)  │  (plugin runner) │
└─────────────────┘                          └──────────────────┘
      │                                              │
      │ 所有核心功能                                  │ 可选插件
      │ - Agent Loop                                │ - TypeScript 插件
      │ - LLM Provider                              │ - 自定义工具
      │ - RPC/HTTP API                              │ - Hook 系统
      │ - Session 管理                              │
      └─────────────────────────────────────────────┘
```

## 总结

✅ **修正**: 用户数据现在存储在 `~/.quantclaw/`
✅ **澄清**: Node.js 是可选的,仅用于插件支持
✅ **简化**: 不再创建专用系统用户
✅ **灵活**: 支持多用户独立使用

核心 C++ 功能完全独立,性能优异,无需任何脚本语言运行时!
