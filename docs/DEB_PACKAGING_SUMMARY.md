# RavBot DEB 打包完成总结

## 已创建的文件

### Debian 打包文件 (debian/)

1. **debian/control** - 包元数据和依赖关系
2. **debian/changelog** - 版本变更日志
3. **debian/rules** - 构建规则(Makefile)
4. **debian/compat** - Debhelper 兼容级别
5. **debian/copyright** - 版权和许可证信息
6. **debian/install** - 额外文件安装规则
7. **debian/postinst** - 安装后脚本(创建用户、初始化)
8. **debian/prerm** - 卸载前脚本(停止服务)
9. **debian/postrm** - 卸载后脚本(清理)
10. **debian/ravbot.service** - Systemd 服务文件
11. **debian/source/format** - 源码包格式

### 构建脚本 (scripts/)

1. **scripts/build-deb.sh** - 自动构建脚本(需要 sudo)
2. **scripts/build-deb-local.sh** - 本地构建脚本(无需 sudo)
3. **scripts/test-deb.sh** - DEB 包测试脚本

### 文档

1. **docs/DEB_PACKAGING.md** - 详细打包指南
2. **INSTALL_DEB.md** - 快速安装指南

### CI/CD

1. **.github/workflows/build-deb.yml** - GitHub Actions 自动构建

## 使用方法

### 快速构建

```bash
# 方法 1: 自动安装依赖(需要 sudo)
./scripts/build-deb.sh

# 方法 2: 手动安装依赖后构建
./scripts/build-deb-local.sh
```

### 安装包

```bash
sudo dpkg -i dist/ravbot_0.3.0-1_amd64.deb
sudo apt-get install -f  # 如果有依赖问题
```

### 测试包

```bash
./scripts/test-deb.sh dist/ravbot_0.3.0-1_amd64.deb
```

## 包特性

### 自动化功能

- ✅ 自动创建系统用户 `ravbot`
- ✅ 自动创建配置目录 `/home/ravbot/.ravbot`
- ✅ 自动安装 Systemd 服务
- ✅ 自动安装 Node.js Sidecar 依赖(如果有 npm)
- ✅ 卸载时自动停止服务
- ✅ Purge 时自动删除用户和配置

### 安装位置

```
/usr/bin/ravbot                          # 主程序
/usr/share/ravbot/skills/                # 内置技能
/usr/share/ravbot/sidecar/               # Node.js Sidecar
/lib/systemd/system/ravbot.service       # Systemd 服务
/usr/share/doc/ravbot/                   # 文档
/home/ravbot/.ravbot/                 # 配置和数据
```

### 依赖关系

**必需依赖:**
- libssl3
- libcurl4
- libsqlite3-0
- zlib1g

**推荐依赖:**
- nodejs (>= 18) - 用于 Plugin Sidecar

## 初始化流程

### 1. 安装包

```bash
sudo dpkg -i dist/ravbot_0.3.0-1_amd64.deb
```

### 2. 运行初始化

```bash
sudo -u ravbot ravbot onboard
```

### 3. 启动服务

```bash
sudo systemctl start ravbot
sudo systemctl enable ravbot
```

### 4. 验证安装

```bash
ravbot --version
ravbot health
sudo systemctl status ravbot
```

## 服务管理

```bash
# 启动
sudo systemctl start ravbot

# 停止
sudo systemctl stop ravbot

# 重启
sudo systemctl restart ravbot

# 查看状态
sudo systemctl status ravbot

# 查看日志
sudo journalctl -u ravbot -f

# 开机自启
sudo systemctl enable ravbot

# 禁用自启
sudo systemctl disable ravbot
```

## 配置管理

```bash
# 查看配置
sudo -u ravbot ravbot config get

# 设置 API 密钥
sudo -u ravbot ravbot config set providers.openai.apiKey "sk-..."
sudo -u ravbot ravbot config set providers.anthropic.apiKey "sk-ant-..."

# 重新加载配置(无需重启)
sudo -u ravbot ravbot config reload
```

## 卸载

```bash
# 保留配置
sudo apt-get remove ravbot

# 完全删除(包括配置和用户)
sudo apt-get purge ravbot
```

## CI/CD 集成

GitHub Actions 工作流已配置,会在以下情况自动构建:

1. 推送 tag (如 `v0.3.0`)
2. 手动触发 workflow

构建产物会自动上传到 GitHub Releases。

## 测试清单

- [ ] 包构建成功
- [ ] 包安装成功
- [ ] 用户和目录自动创建
- [ ] Systemd 服务正常启动
- [ ] 命令行工具可用
- [ ] Dashboard 可访问
- [ ] 配置管理正常
- [ ] 卸载清理完整

## 已知限制

1. 需要 Ubuntu 24.04 LTS (Noble Numbat)
2. 仅支持 amd64 和 arm64 架构
3. Node.js Sidecar 依赖需要手动安装 Node.js >= 18
4. 首次运行需要执行 `ravbot onboard` 初始化

## 后续改进

- [ ] 支持更多 Ubuntu 版本 (22.04, 23.10)
- [ ] 支持 Debian 系列
- [ ] 添加 AppArmor/SELinux 配置
- [ ] 提供预编译的二进制包下载
- [ ] 添加 PPA 仓库支持
- [ ] 多架构交叉编译

## 参考文档

- [Debian 打包指南](https://www.debian.org/doc/manuals/maint-guide/)
- [Ubuntu 打包指南](https://packaging.ubuntu.com/html/)
- [Systemd 服务配置](https://www.freedesktop.org/software/systemd/man/systemd.service.html)

## 支持

如有问题,请访问:
- GitHub Issues: https://github.com/RavBot/RavBot/issues
- 文档: https://ravbot.github.io
