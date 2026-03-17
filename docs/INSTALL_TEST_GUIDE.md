# Ubuntu 24.04 全新系统安装测试指南

## 问题已修复

**原问题:**
```
dpkg: 处理归档时出错：
 无法打开 /usr/lib/systemd/system/ravbot.service.dpkg-new: 没有那个文件或目录
```

**修复内容:**
- ✅ 修正 systemd 服务文件路径
- ✅ 使用 dh_installsystemd 自动处理
- ✅ 添加包验证脚本

## 新包信息

**文件:** `dist/ravbot_0.3.0-1_amd64.deb`
**大小:** 11MB
**SHA256:** `b3dadb6dc25e7fce177dcf150fe36be9d11470bd20256a8660ca5ea07e4dfbbb`

## 在全新 Ubuntu 24.04 上安装

### 1. 下载包

```bash
# 方法 1: 从构建机器复制
scp user@build-machine:/path/to/RavBot/dist/ravbot_0.3.0-1_amd64.deb ~/

# 方法 2: 从 GitHub Releases 下载 (发布后)
wget https://github.com/RavBot/RavBot/releases/download/v0.3.0/ravbot_0.3.0-1_amd64.deb
```

### 2. 验证包 (可选但推荐)

```bash
# 验证 SHA256
echo "b3dadb6dc25e7fce177dcf150fe36be9d11470bd20256a8660ca5ea07e4dfbbb  ravbot_0.3.0-1_amd64.deb" | sha256sum -c

# 查看包内容
dpkg -c ravbot_0.3.0-1_amd64.deb | less

# 查看包信息
dpkg-deb -I ravbot_0.3.0-1_amd64.deb
```

### 3. 安装

```bash
# 安装包
sudo dpkg -i ravbot_0.3.0-1_amd64.deb

# 自动安装依赖 (包括 nodejs >= 18)
sudo apt-get install -f
```

**预期输出:**
```
正在选中未选择的软件包 ravbot。
正在解压 ravbot (0.3.0-1) ...
正在设置 ravbot (0.3.0-1) ...
✓ Sidecar installed successfully

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  RavBot has been installed successfully!
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Next steps:
  1. Run the onboarding wizard:
     ravbot onboard

  2. Start the gateway:
     ravbot gateway

  3. Or install as system service:
     ravbot gateway install
     systemctl start ravbot
     systemctl enable ravbot

Configuration will be stored in: ~/.ravbot/

Plugin system (Node.js Sidecar) is ready for OpenClaw plugins.

For more information, visit: https://ravbot.github.io
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

### 4. 验证安装

```bash
# 检查版本
ravbot --version
# 输出: ravbot 0.3.0 (build ...)

# 检查文件
ls -la /usr/bin/ravbot
ls -la /usr/lib/systemd/system/ravbot.service
ls -la /usr/share/ravbot/sidecar/dist/index.js
ls -la /usr/share/ravbot/skills/

# 检查 Node.js
node --version
# 应该 >= 18
```

### 5. 初始化配置

```bash
# 运行初始化向导
ravbot onboard
```

**交互式配置:**
1. 选择 LLM Provider (OpenAI/Anthropic/Google/Qwen)
2. 输入 API 密钥
3. 选择默认模型
4. 完成配置

### 6. 启动服务

**方式 1: 前台运行 (推荐用于测试)**
```bash
ravbot gateway
```

**方式 2: 系统服务 (推荐用于生产)**
```bash
# 安装服务
ravbot gateway install

# 启动
sudo systemctl start ravbot

# 开机自启
sudo systemctl enable ravbot

# 查看状态
sudo systemctl status ravbot

# 查看日志
journalctl -u ravbot -f
```

### 7. 测试功能

```bash
# 健康检查
ravbot health

# 发送测试消息
ravbot agent "你好，请介绍一下自己"

# 打开 Web 控制台
ravbot dashboard
# 浏览器访问: http://localhost:18801
```

## 验证 Sidecar (插件系统)

### 检查 Sidecar 文件

```bash
# 检查编译后的 JavaScript
ls -la /usr/share/ravbot/sidecar/dist/
# 应该看到: index.js, plugin-loader.js, rpc-server.js 等

# 检查 Node.js 依赖
ls -la /usr/share/ravbot/sidecar/node_modules/
# 应该看到: jiti/ 等目录

# 检查包配置
cat /usr/share/ravbot/sidecar/package.json
```

### 测试插件功能

```bash
# 1. 创建测试插件
mkdir -p ~/.ravbot/plugins/test-plugin

cat > ~/.ravbot/plugins/test-plugin/ravbot.plugin.json << 'EOF'
{
  "name": "test-plugin",
  "version": "1.0.0",
  "main": "index.js"
}
EOF

cat > ~/.ravbot/plugins/test-plugin/index.js << 'EOF'
export const tools = [{
  name: "test_tool",
  description: "测试工具",
  input_schema: {
    type: "object",
    properties: {
      message: { type: "string" }
    }
  },
  execute: async (input) => {
    return `收到消息: ${input.message}`;
  }
}];
EOF

# 2. 启用插件
ravbot config set plugins.allow '["test-plugin"]'

# 3. 重启服务
ravbot gateway restart

# 4. 验证插件
ravbot plugins list

# 5. 测试插件工具
ravbot agent "使用 test_tool 发送消息: Hello"
```

## 故障排查

### 问题 1: 安装失败 - 依赖缺失

```bash
# 错误: 依赖关系问题
sudo apt-get install -f

# 手动安装 Node.js (如果需要)
sudo apt-get install nodejs npm
node --version  # 确认 >= 18
```

### 问题 2: systemd 服务无法启动

```bash
# 查看详细日志
journalctl -u ravbot -n 50 --no-pager

# 检查服务文件
cat /usr/lib/systemd/system/ravbot.service

# 重新加载 systemd
sudo systemctl daemon-reload

# 手动运行 (查看错误)
ravbot gateway
```

### 问题 3: Sidecar 无法启动

```bash
# 检查 Node.js 版本
node --version  # 必须 >= 18

# 检查 Sidecar 文件
ls -la /usr/share/ravbot/sidecar/dist/
ls -la /usr/share/ravbot/sidecar/node_modules/

# 手动测试 Sidecar
export RAVBOT_PORT=18802
node /usr/share/ravbot/sidecar/dist/index.js
```

### 问题 4: 端口冲突

```bash
# 检查端口占用
sudo netstat -tlnp | grep -E '18800|18801'

# 修改端口
ravbot config set gateway.port 19000
ravbot config set gateway.controlUi.port 19001
```

### 问题 5: 插件加载失败

```bash
# 检查插件配置
cat ~/.ravbot/ravbot.json | grep -A 5 plugins

# 检查插件目录
ls -la ~/.ravbot/plugins/

# 启用详细日志
RAVBOT_VERBOSE=1 ravbot gateway
```

## 卸载

```bash
# 停止服务
sudo systemctl stop ravbot
sudo systemctl disable ravbot

# 卸载包 (保留配置)
sudo apt-get remove ravbot

# 完全删除 (包括配置)
sudo apt-get purge ravbot
rm -rf ~/.ravbot
```

## 反馈问题

如果遇到任何问题,请提供以下信息:

```bash
# 1. 系统信息
lsb_release -a
uname -a

# 2. 包信息
dpkg -l | grep ravbot

# 3. 依赖信息
dpkg -l | grep -E "nodejs|libssl|libcurl|libsqlite"

# 4. 服务状态
systemctl status ravbot

# 5. 日志
journalctl -u ravbot -n 100 --no-pager

# 6. 配置
cat ~/.ravbot/ravbot.json
```

提交 Issue: https://github.com/RavBot/RavBot/issues

## 成功标志

安装成功后,你应该能够:

- ✅ 运行 `ravbot --version` 看到版本号
- ✅ 运行 `ravbot health` 看到 "Gateway: ok"
- ✅ 运行 `ravbot agent "test"` 收到 AI 回复
- ✅ 访问 http://localhost:18801 看到 Web 控制台
- ✅ 看到 Sidecar 文件在 `/usr/share/ravbot/sidecar/`
- ✅ 创建和加载插件成功

---

**祝使用愉快!** 🎉
