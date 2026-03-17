# RavBot 工具执行故障排查指南

## 问题描述

在 Telegram Bot 中执行系统命令时收到错误: "当前环境中命令执行功能被限制了"

## 根本原因

配置文件 `~/.ravbot/ravbot.json` 中的工具白名单为空:

```json
"tools": {
  "allow": [],  // 空白名单阻止所有工具
  "exec": {
    "ask": "on-miss"
  },
  "profile": "coding"
}
```

## 解决方案

### 方案 1: 自动修复 (推荐)

运行诊断脚本:

```bash
bash scripts/diagnose_tools.sh
```

选择选项 1 (允许所有工具) 或选项 2 (允许特定工具)。

### 方案 2: 手动修复

#### 2.1 允许所有工具 (开发环境推荐)

编辑 `~/.ravbot/ravbot.json`:

```json
"tools": {
  "allow": ["*"],  // 允许所有工具
  "exec": {
    "ask": "on-miss"
  },
  "profile": "coding"
}
```

#### 2.2 允许特定工具 (生产环境推荐)

```json
"tools": {
  "allow": [
    "bash",
    "exec",
    "read_file",
    "write_file",
    "list_dir",
    "search_files",
    "web_search"
  ],
  "exec": {
    "ask": "on-miss"
  },
  "profile": "coding"
}
```

### 方案 3: 使用 jq 命令修复

```bash
# 备份配置
cp ~/.ravbot/ravbot.json ~/.ravbot/ravbot.json.backup

# 允许所有工具
jq '.tools.allow = ["*"]' ~/.ravbot/ravbot.json > ~/.ravbot/ravbot.json.tmp
mv ~/.ravbot/ravbot.json.tmp ~/.ravbot/ravbot.json
```

## 重启服务

修复配置后,重启 RavBot 服务:

```bash
# 如果使用 systemd
systemctl --user restart ravbot

# 或者手动重启
pkill ravbot
ravbot daemon start
```

## 验证修复

### 1. 检查配置

```bash
jq '.tools' ~/.ravbot/ravbot.json
```

应该看到:

```json
{
  "allow": [
    "*"
  ],
  "exec": {
    "ask": "on-miss"
  },
  "profile": "coding"
}
```

### 2. 测试命令执行

在 Telegram 中发送:

```
请执行 uptime
```

或

```
请检查系统健康
```

应该能正常执行并返回结果。

### 3. 查看日志

```bash
# 查看实时日志
journalctl --user -u ravbot -f

# 或查看最近日志
tail -f ~/.ravbot/logs/ravbot.log
```

## 工具白名单说明

### allow: ["*"]

- **含义**: 允许所有工具
- **适用**: 开发环境、个人使用
- **风险**: 较低 (仍有沙箱保护)

### allow: [具体工具列表]

- **含义**: 仅允许列表中的工具
- **适用**: 生产环境、多用户环境
- **风险**: 最低

### allow: []

- **含义**: 禁止所有工具
- **适用**: 仅聊天场景,不需要工具调用
- **风险**: 无 (但功能受限)

## 常用工具列表

| 工具名 | 功能 | 风险级别 |
|--------|------|----------|
| bash | 执行 shell 命令 | 中 |
| exec | 执行 shell 命令 (别名) | 中 |
| read_file | 读取文件 | 低 |
| write_file | 写入文件 | 中 |
| list_dir | 列出目录 | 低 |
| search_files | 搜索文件 | 低 |
| web_search | 网络搜索 | 低 |
| browser | 浏览器控制 | 中 |

## 安全考虑

### 沙箱保护

即使允许所有工具,RavBot 仍有多层安全保护:

1. **命令验证**: `SecuritySandbox::ValidateShellCommand()`
   - 阻止 `rm -rf /`
   - 阻止 `mkfs`
   - 阻止 `dd if=`

2. **资源限制**: `SecuritySandbox::ApplyResourceLimits()`
   - CPU 时间限制
   - 内存限制
   - 文件大小限制

3. **执行审批**: `ExecApprovalManager`
   - 可配置的命令审批流程
   - `"ask": "on-miss"` 表示未知命令需要确认

4. **审计日志**: `SecurityAuditLogger`
   - 记录所有工具调用
   - 记录危险操作

### 推荐配置

#### 开发环境

```json
"tools": {
  "allow": ["*"],
  "exec": {
    "ask": "on-miss"
  },
  "profile": "coding"
}
```

#### 生产环境

```json
"tools": {
  "allow": [
    "read_file",
    "list_dir",
    "search_files",
    "web_search"
  ],
  "exec": {
    "ask": "always"
  },
  "profile": "safe"
}
```

## 常见问题

### Q1: 修复后仍然无法执行命令

**A**: 确保已重启服务:

```bash
systemctl --user restart ravbot
```

### Q2: 特定命令被阻止

**A**: 检查是否触发了沙箱规则:

```bash
grep "Command not allowed" ~/.ravbot/logs/ravbot.log
```

### Q3: 如何查看可用工具列表

**A**: 在 Telegram 中发送:

```
列出所有可用工具
```

或查看代码:

```bash
grep "register_tool" src/tools/tool_registry.cpp
```

### Q4: 如何临时禁用工具

**A**: 设置 `allow: []` 并重启服务。

## 相关文件

- **配置文件**: `~/.ravbot/ravbot.json`
- **诊断脚本**: `scripts/diagnose_tools.sh`
- **工具注册**: `src/tools/tool_registry.cpp`
- **沙箱实现**: `src/security/sandbox.cpp`
- **日志文件**: `~/.ravbot/logs/ravbot.log`

## 参考文档

- [Configuration Guide](../docs/CONFIGURATION_GUIDE.md)
- [API Reference](../docs/API_REFERENCE.md)
- [Security Documentation](../docs/SECURITY.md)

## 版本信息

- **适用版本**: v0.3.0+
- **最后更新**: 2026-03-14
