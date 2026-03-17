# Telegram Bot 连接问题诊断报告

**报告时间:** 2026-03-16 18:46
**问题:** Telegram bot (@amdravbotbot) 无响应

## 问题分析

### 1. 网络连接测试结果

**远程机器 (192.168.1.104) 网络状态:**

```bash
# ✅ 基本网络连接正常
ping 8.8.8.8  # 成功，延迟 165-174ms

# ✅ 国内网站访问正常
curl https://www.baidu.com  # 成功

# ❌ Telegram API 无法连接
curl https://api.telegram.org/bot.../getMe
# 错误: Connection timed out after 10002 milliseconds
# DNS 解析成功: IPv4 128.242.245.29, IPv6 2a03:2880:f10a:83:face:b00c:0:25de
# 但 TCP 连接超时

# ❌ 本地 API 端点无法连接
curl http://192.168.1.131:8991
# 错误: Failed to connect to 192.168.1.131 port 8991
```

### 2. 根本原因

**问题 1: Telegram API 被屏蔽**
- 远程机器可以解析 Telegram 域名
- 但无法建立 TCP 连接（超时）
- 可能原因：
  - 网络运营商屏蔽 Telegram
  - 防火墙规则阻止
  - 需要代理访问

**问题 2: 跨机器 API 访问失败**
- 本地 API 服务运行在 `0.0.0.0:8991`
- 但远程机器无法通过 `192.168.1.131:8991` 访问
- 可能原因：
  - 本地防火墙阻止外部访问
  - 路由配置问题

## 当前配置状态

### 本地机器 (192.168.1.131)
```json
{
  "channels": {
    "telegram": {
      "enabled": true,
      "botToken": "8208097744:AAFZ9qFR5wJjaxQPVt5BI4LDKNQ6ZXTLyyI"
    }
  },
  "models": {
    "providers": {
      "anthropic": {
        "baseUrl": "http://127.0.0.1:8991",
        "apiKey": "sk-s0n19ykdar6jpnbdma79zrw15wbubba28t9to2kkp5y7llqp"
      }
    }
  }
}
```

### 远程机器 (192.168.1.104)
```json
{
  "channels": {
    "telegram": {
      "enabled": false  // 已禁用，因为无法连接 Telegram API
    }
  },
  "models": {
    "providers": {
      "anthropic": {
        "baseUrl": "http://127.0.0.1:8991",  // 改为本地，但本地没有 API 服务
        "apiKey": "sk-s0n19ykdar6jpnbdma79zrw15wbubba28t9to2kkp5y7llqp"
      }
    }
  }
}
```

**Gateway 状态:**
- 端口: 18802 (默认 18800 被占用)
- HTTP API: 18803 (默认 18801 被占用)
- 状态: 运行中

## 解决方案

### 方案 1: 本地运行 Telegram Bot（推荐）

在本地机器运行 Telegram bot，通过 SSH 隧道连接到远程 Gateway：

```bash
# 1. 在本地创建 SSH 隧道
ssh -L 18802:127.0.0.1:18802 kaifa@192.168.1.104

# 2. 修改本地配置，连接到隧道
{
  "gateway": {
    "port": 18802  // 通过隧道连接到远程
  },
  "channels": {
    "telegram": {
      "enabled": true,
      "botToken": "8725555597:AAGZ04ZfnkSrEbI57kVFH7-32nBQycPcdKI"
    }
  }
}

# 3. 启动本地 Gateway（连接到远程）
ravbot gateway
```

**优点:**
- 本地可以访问 Telegram API
- 通过 SSH 隧道安全连接到远程
- 不需要修改防火墙规则

### 方案 2: 配置代理

在远程机器配置 HTTP 代理访问 Telegram：

```bash
# 1. 安装代理客户端（如 v2ray, clash 等）
# 2. 配置环境变量
export HTTP_PROXY=http://proxy-server:port
export HTTPS_PROXY=http://proxy-server:port

# 3. 重启 Gateway
ravbot gateway
```

### 方案 3: 开放本地 API 端口

让远程机器直接访问本地 API：

```bash
# 1. 在本地机器开放防火墙
sudo ufw allow 8991/tcp

# 2. 修改远程配置
{
  "models": {
    "providers": {
      "anthropic": {
        "baseUrl": "http://192.168.1.131:8991"
      }
    }
  }
}
```

**缺点:**
- 仍然无法解决 Telegram API 访问问题
- 需要修改防火墙规则

## 推荐操作步骤

**立即可行的方案（方案 1）:**

1. 在本地机器创建 SSH 隧道到远程 Gateway
2. 在本地运行 Telegram bot，连接到隧道
3. 测试 bot 响应

**命令:**

```bash
# 终端 1: 创建隧道
ssh -N -L 18802:127.0.0.1:18802 kaifa@192.168.1.104

# 终端 2: 修改本地配置并启动
# (需要修改 ravbot.json)
ravbot gateway
```

## 测试验证

成功后应该能够：
- ✅ 在 Telegram 与 @amdravbotbot 对话
- ✅ Bot 能够响应消息
- ✅ 消息通过本地 Gateway → SSH 隧道 → 远程 Gateway 处理
- ✅ 远程 Gateway 使用本地 API 生成回复

---

**状态:** 待实施
**优先级:** 高
**预计时间:** 10 分钟
