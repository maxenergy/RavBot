# RavBot 快速上手指南

## 📦 安装

### Ubuntu 24.04 (推荐)

```bash
# 1. 下载 DEB 包
wget https://github.com/RavBot/RavBot/releases/latest/download/ravbot_0.3.0-1_amd64.deb

# 2. 安装
sudo dpkg -i ravbot_0.3.0-1_amd64.deb
sudo apt-get install -f  # 自动安装依赖

# 3. 验证安装
ravbot --version
```

### 从源码构建

```bash
git clone https://github.com/RavBot/RavBot.git
cd RavBot
./scripts/build-deb.sh
sudo dpkg -i dist/ravbot_*.deb
```

## 🚀 快速开始

### 1. 初始化配置

```bash
ravbot onboard
```

交互式向导会引导你完成:
- ✅ 创建配置目录 `~/.ravbot/`
- ✅ 选择 LLM Provider (OpenAI/Anthropic/Google/Qwen)
- ✅ 配置 API 密钥
- ✅ 设置默认模型

### 2. 启动服务

**前台运行 (推荐用于测试):**
```bash
ravbot gateway
```

**后台服务 (推荐用于生产):**
```bash
# 安装为系统服务
ravbot gateway install

# 启动服务
sudo systemctl start ravbot

# 开机自启
sudo systemctl enable ravbot

# 查看状态
sudo systemctl status ravbot

# 查看日志
journalctl -u ravbot -f
```

### 3. 验证运行

```bash
# 健康检查
ravbot health

# 查看状态
ravbot status

# 打开 Web 控制台
ravbot dashboard
# 浏览器访问: http://localhost:18801
```

## ⚙️ 配置 API 密钥

### OpenAI

```bash
ravbot config set providers.openai.apiKey "sk-..."
ravbot config set providers.openai.model "gpt-4"
```

### Anthropic (Claude)

```bash
ravbot config set providers.anthropic.apiKey "sk-ant-..."
ravbot config set providers.anthropic.model "claude-3-5-sonnet-20241022"
```

### Google (Gemini)

```bash
ravbot config set providers.google.apiKey "AIza..."
ravbot config set providers.google.model "gemini-2.0-flash-exp"
```

### Qwen (通义千问)

```bash
ravbot config set providers.qwen.apiKey "sk-..."
ravbot config set providers.qwen.model "qwen-max"
```

### 查看配置

```bash
# 查看所有配置
ravbot config get

# 查看特定配置
ravbot config get providers.openai

# 重新加载配置
ravbot config reload
```

## 💬 使用 Agent

### 命令行交互

```bash
# 发送单条消息
ravbot agent "你好，请介绍一下自己"

# 继续对话 (使用上次会话)
ravbot agent "帮我写一个 Python 快速排序"

# 指定会话 ID
ravbot agent --session my-session "分析这段代码"

# 使用特定模型
ravbot agent --model gpt-4 "复杂的推理任务"
```

### WebSocket 客户端

```javascript
const ws = new WebSocket('ws://localhost:18800');

ws.onopen = () => {
  ws.send(JSON.stringify({
    jsonrpc: "2.0",
    method: "agent.send_message",
    params: {
      message: "你好",
      session_id: "my-session"
    },
    id: 1
  }));
};

ws.onmessage = (event) => {
  const response = JSON.parse(event.data);
  console.log(response.result);
};
```

### HTTP REST API

```bash
# 发送消息
curl -X POST http://localhost:18801/api/agent/message \
  -H "Content-Type: application/json" \
  -d '{
    "message": "你好",
    "session_id": "my-session"
  }'

# 查看会话列表
curl http://localhost:18801/api/sessions

# 查看会话历史
curl http://localhost:18801/api/sessions/my-session/messages
```

## 📋 会话管理

```bash
# 列出所有会话
ravbot sessions list

# 查看会话详情
ravbot sessions show my-session

# 删除会话
ravbot sessions delete my-session

# 导出会话
ravbot sessions export my-session > session.json

# 导入会话
ravbot sessions import < session.json
```

## 🔌 插件系统

### 启用插件

```bash
# 1. 创建插件目录
mkdir -p ~/.ravbot/plugins/my-plugin

# 2. 创建插件配置
cat > ~/.ravbot/plugins/my-plugin/ravbot.plugin.json << 'EOF'
{
  "name": "my-plugin",
  "version": "1.0.0",
  "description": "我的自定义插件",
  "main": "index.js"
}
EOF

# 3. 创建插件代码
cat > ~/.ravbot/plugins/my-plugin/index.js << 'EOF'
export const tools = [{
  name: "my_tool",
  description: "我的自定义工具",
  input_schema: {
    type: "object",
    properties: {
      text: { type: "string", description: "输入文本" }
    },
    required: ["text"]
  },
  execute: async (input) => {
    return `处理结果: ${input.text}`;
  }
}];
EOF

# 4. 启用插件
ravbot config set plugins.allow '["my-plugin"]'

# 5. 重启服务
ravbot gateway restart

# 6. 验证插件
ravbot plugins list
curl http://localhost:18801/api/plugins/tools
```

### 插件示例

**天气查询插件:**
```javascript
// ~/.ravbot/plugins/weather/index.js
export const tools = [{
  name: "get_weather",
  description: "查询城市天气",
  input_schema: {
    type: "object",
    properties: {
      city: { type: "string", description: "城市名称" }
    },
    required: ["city"]
  },
  execute: async (input) => {
    // 调用天气 API
    const response = await fetch(
      `https://api.weatherapi.com/v1/current.json?key=YOUR_KEY&q=${input.city}`
    );
    const data = await response.json();
    return `${input.city}天气: ${data.current.condition.text}, ${data.current.temp_c}°C`;
  }
}];
```

**数据库查询插件:**
```javascript
// ~/.ravbot/plugins/database/index.js
import sqlite3 from 'sqlite3';

export const tools = [{
  name: "query_db",
  description: "执行 SQL 查询",
  input_schema: {
    type: "object",
    properties: {
      sql: { type: "string", description: "SQL 查询语句" }
    },
    required: ["sql"]
  },
  execute: async (input) => {
    const db = new sqlite3.Database('./data.db');
    return new Promise((resolve, reject) => {
      db.all(input.sql, (err, rows) => {
        if (err) reject(err);
        else resolve(JSON.stringify(rows, null, 2));
      });
    });
  }
}];
```

## 🛠️ 常用配置

### 修改端口

```bash
# WebSocket RPC 端口 (默认 18800)
ravbot config set gateway.port 19000

# HTTP API 端口 (默认 18801)
ravbot config set gateway.controlUi.port 19001

# 重启服务
ravbot gateway restart
```

### 配置日志级别

```bash
# 设置日志级别: trace, debug, info, warn, error
ravbot config set logging.level "debug"

# 查看日志
ravbot logs

# 或使用系统日志
journalctl -u ravbot -f
```

### 配置工作区

```bash
# 设置工作区目录
ravbot config set workspace.path "/path/to/workspace"

# 设置最大会话数
ravbot config set workspace.maxSessions 100

# 设置会话超时 (秒)
ravbot config set workspace.sessionTimeout 3600
```

### 配置安全选项

```bash
# 启用 RBAC
ravbot config set security.rbac.enabled true

# 配置速率限制
ravbot config set security.rateLimit.enabled true
ravbot config set security.rateLimit.maxRequests 100
ravbot config set security.rateLimit.windowSeconds 60

# 启用沙箱
ravbot config set security.sandbox.enabled true
```

## 📊 监控和调试

### 查看系统状态

```bash
# 健康检查
ravbot health

# 详细状态
ravbot status

# 查看进程
ps aux | grep ravbot

# 查看端口
sudo netstat -tlnp | grep ravbot
```

### 查看日志

```bash
# 应用日志
ravbot logs

# 系统服务日志
journalctl -u ravbot -f

# 查看最近 100 行
journalctl -u ravbot -n 100

# 查看错误日志
journalctl -u ravbot -p err
```

### 性能监控

```bash
# 查看资源使用
top -p $(pgrep ravbot)

# 内存使用
ps aux | grep ravbot | awk '{print $6}'

# 查看连接数
netstat -an | grep :18800 | wc -l
```

## 🔧 故障排查

### 服务无法启动

```bash
# 1. 查看详细日志
journalctl -u ravbot -n 50 --no-pager

# 2. 检查配置
ravbot config get

# 3. 手动运行 (查看错误)
ravbot gateway

# 4. 检查端口占用
sudo netstat -tlnp | grep -E '18800|18801'
```

### 端口冲突

```bash
# 查找占用进程
sudo lsof -i :18800

# 修改端口
ravbot config set gateway.port 19000
ravbot config set gateway.controlUi.port 19001
```

### API 密钥无效

```bash
# 重新设置密钥
ravbot config set providers.openai.apiKey "sk-..."

# 测试连接
ravbot agent "test"

# 查看详细错误
RAVBOT_VERBOSE=1 ravbot gateway
```

### Sidecar 无法启动

```bash
# 检查 Node.js 版本
node --version  # 应该 >= 18

# 检查 Sidecar 文件
ls -la /usr/share/ravbot/sidecar/dist/
ls -la /usr/share/ravbot/sidecar/node_modules/

# 手动测试 Sidecar
export RAVBOT_PORT=18802
node /usr/share/ravbot/sidecar/dist/index.js
```

### 插件加载失败

```bash
# 检查插件配置
cat ~/.ravbot/ravbot.json | grep -A 5 plugins

# 检查插件目录
ls -la ~/.ravbot/plugins/

# 查看插件列表
ravbot plugins list

# 启用详细日志
RAVBOT_VERBOSE=1 ravbot gateway
```

## 📚 配置文件示例

### 完整配置 (~/.ravbot/ravbot.json)

```json
{
  "providers": {
    "openai": {
      "apiKey": "sk-...",
      "model": "gpt-4",
      "baseUrl": "https://api.openai.com/v1"
    },
    "anthropic": {
      "apiKey": "sk-ant-...",
      "model": "claude-3-5-sonnet-20241022"
    }
  },
  "gateway": {
    "port": 18800,
    "controlUi": {
      "enabled": true,
      "port": 18801
    }
  },
  "workspace": {
    "path": "~/.ravbot/agents/main/workspace",
    "maxSessions": 100,
    "sessionTimeout": 3600
  },
  "plugins": {
    "allow": ["weather", "database"],
    "deny": []
  },
  "security": {
    "rbac": {
      "enabled": true
    },
    "rateLimit": {
      "enabled": true,
      "maxRequests": 100,
      "windowSeconds": 60
    },
    "sandbox": {
      "enabled": true
    }
  },
  "logging": {
    "level": "info",
    "file": "~/.ravbot/logs/ravbot.log"
  }
}
```

## 🔗 相关资源

- **官方文档**: https://ravbot.github.io
- **GitHub**: https://github.com/RavBot/RavBot
- **问题反馈**: https://github.com/RavBot/RavBot/issues
- **DEB 打包指南**: [docs/DEB_PACKAGING.md](docs/DEB_PACKAGING.md)
- **Sidecar 指南**: [docs/SIDECAR_GUIDE.md](docs/SIDECAR_GUIDE.md)
- **完整安装指南**: [INSTALL_DEB.md](INSTALL_DEB.md)

## 💡 使用技巧

### 1. 使用别名简化命令

```bash
# 添加到 ~/.bashrc 或 ~/.zshrc
alias qc='ravbot'
alias qca='ravbot agent'
alias qcg='ravbot gateway'
alias qcs='ravbot sessions'
alias qcc='ravbot config'

# 使用
qca "你好"
qcs list
qcc get
```

### 2. 多模型对比

```bash
# 同一问题用不同模型回答
ravbot agent --model gpt-4 "解释量子计算" > gpt4.txt
ravbot agent --model claude-3-5-sonnet-20241022 "解释量子计算" > claude.txt
diff gpt4.txt claude.txt
```

### 3. 批量处理

```bash
# 批量发送消息
cat questions.txt | while read line; do
  ravbot agent "$line" >> answers.txt
done
```

### 4. 定时任务

```bash
# 每小时执行健康检查
crontab -e
# 添加: 0 * * * * /usr/bin/ravbot health >> /var/log/ravbot-health.log
```

### 5. 远程访问

```bash
# 配置允许远程访问
ravbot config set gateway.host "0.0.0.0"

# 使用 nginx 反向代理
# /etc/nginx/sites-available/ravbot
server {
    listen 80;
    server_name ravbot.example.com;

    location / {
        proxy_pass http://localhost:18801;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
    }
}
```

## 🎯 下一步

- 📖 阅读 [完整文档](https://ravbot.github.io)
- 🔌 开发自定义插件 ([SIDECAR_GUIDE.md](docs/SIDECAR_GUIDE.md))
- 🤝 参与贡献 ([CONTRIBUTING.md](CONTRIBUTING.md))
- 💬 加入社区讨论

---

**需要帮助?** 访问 [GitHub Issues](https://github.com/RavBot/RavBot/issues) 或查看 [故障排查指南](#故障排查)
