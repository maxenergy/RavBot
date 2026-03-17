# RavBot DEB 包测试清单

## 📋 测试环境

- [ ] 全新 Ubuntu 24.04 系统
- [ ] 网络连接正常
- [ ] 有 sudo 权限

## 📦 包传输

- [ ] 包已传输到测试机器: `~/下载/ravbot_0.3.0-1_amd64.deb`
- [ ] SHA256 验证通过: `b3dadb6dc25e7fce177dcf150fe36be9d11470bd20256a8660ca5ea07e4dfbbb`

## 🔧 安装测试

- [ ] `sudo dpkg -i ravbot_0.3.0-1_amd64.deb` 成功
- [ ] `sudo apt-get install -f` 自动安装依赖
- [ ] 看到成功消息: "RavBot has been installed successfully!"
- [ ] 看到 Sidecar 消息: "✓ Sidecar installed successfully"

## ✅ 基础验证

- [ ] `ravbot --version` 显示版本号
- [ ] `/usr/bin/ravbot` 文件存在
- [ ] `/usr/lib/systemd/system/ravbot.service` 文件存在 (不是 /lib/systemd/)
- [ ] `/usr/share/ravbot/sidecar/dist/index.js` 文件存在
- [ ] `/usr/share/ravbot/sidecar/node_modules/` 目录存在
- [ ] `node --version` 显示 >= 18

## 🚀 功能测试

- [ ] `ravbot onboard` 初始化成功
- [ ] `ravbot gateway` 启动成功
- [ ] `ravbot health` 显示 "Gateway: ok"
- [ ] `ravbot agent "你好"` 收到 AI 回复
- [ ] 访问 http://localhost:18801 看到 Web 控制台

## 🔌 插件测试

- [ ] 创建测试插件目录: `~/.ravbot/plugins/test-plugin`
- [ ] 创建插件配置文件
- [ ] 创建插件代码文件
- [ ] `ravbot config set plugins.allow '["test-plugin"]'` 成功
- [ ] `ravbot gateway restart` 成功
- [ ] `ravbot plugins list` 显示测试插件
- [ ] 插件工具可以正常调用

## 📊 系统服务测试

- [ ] `ravbot gateway install` 安装服务成功
- [ ] `sudo systemctl start ravbot` 启动成功
- [ ] `sudo systemctl status ravbot` 显示 active (running)
- [ ] `journalctl -u ravbot -n 20` 无错误日志
- [ ] `sudo systemctl enable ravbot` 设置开机自启成功

## 🔍 Sidecar 验证

- [ ] `ls -la /usr/share/ravbot/sidecar/dist/` 显示编译后的 JS 文件
- [ ] `ls -la /usr/share/ravbot/sidecar/node_modules/jiti/` 存在
- [ ] `cat /usr/share/ravbot/sidecar/package.json` 可读取
- [ ] 插件系统正常工作 (上面已测试)

## 🧹 卸载测试 (可选)

- [ ] `sudo systemctl stop ravbot` 停止服务
- [ ] `sudo systemctl disable ravbot` 禁用自启
- [ ] `sudo apt-get remove ravbot` 卸载成功
- [ ] `sudo apt-get purge ravbot` 完全删除
- [ ] `rm -rf ~/.ravbot` 删除用户数据

## ❌ 问题记录

如果遇到问题,请记录:

**问题描述:**


**错误信息:**


**系统信息:**
```bash
lsb_release -a
dpkg -l | grep ravbot
journalctl -u ravbot -n 50
```

**截图:**


## ✅ 测试结论

- [ ] 所有测试通过 ✅
- [ ] 部分测试失败 ⚠️ (请在上面记录问题)
- [ ] 测试失败 ❌ (请提供详细信息)

---

**测试人员:**
**测试日期:**
**测试环境:** Ubuntu 24.04
**包版本:** ravbot_0.3.0-1_amd64.deb
