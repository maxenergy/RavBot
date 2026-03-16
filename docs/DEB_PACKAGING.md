# QuantClaw DEB 打包指南

## 快速开始

### 一键构建

```bash
./scripts/build-deb.sh
```

这将自动:
1. 安装构建依赖
2. 编译 C++ 代码
3. 打包成 DEB 文件
4. 生成 SHA256 校验和
5. 输出到 `dist/` 目录

### 安装包

```bash
# 安装 DEB 包
sudo dpkg -i dist/quantclaw_0.3.0-1_amd64.deb

# 如果有依赖问题,运行:
sudo apt-get install -f

# 验证安装
quantclaw --version
```

## 包结构

### 文件布局

```
/usr/bin/quantclaw                          # 主程序
/usr/share/quantclaw/skills/                # 内置技能
/usr/share/quantclaw/sidecar/               # Node.js Sidecar
/lib/systemd/system/quantclaw.service       # Systemd 服务
/usr/share/doc/quantclaw/                   # 文档
```

### 用户和权限

- 系统用户: `quantclaw`
- 主目录: `/home/quantclaw`
- 配置目录: `/home/quantclaw/.quantclaw`

## 安装后配置

### 1. 运行初始化向导

```bash
sudo -u quantclaw quantclaw onboard
```

### 2. 启动服务

```bash
# 启动服务
sudo systemctl start quantclaw

# 开机自启
sudo systemctl enable quantclaw

# 查看状态
sudo systemctl status quantclaw

# 查看日志
sudo journalctl -u quantclaw -f
```

### 3. 访问 Dashboard

```bash
# 打开 Web UI
quantclaw dashboard

# 或直接访问
http://localhost:18801
```

## 手动构建步骤

如果需要手动控制构建过程:

### 1. 安装构建依赖

```bash
sudo apt-get update
sudo apt-get install -y \
    dpkg-dev debhelper devscripts \
    build-essential cmake \
    libssl-dev libcurl4-openssl-dev \
    nlohmann-json3-dev libspdlog-dev \
    zlib1g-dev libsqlite3-dev
```

### 2. 构建包

```bash
# 在项目根目录
dpkg-buildpackage -us -uc -b
```

### 3. 查看生成的文件

```bash
ls -lh ../quantclaw_*.deb
```

## 卸载

### 保留配置

```bash
sudo apt-get remove quantclaw
```

### 完全删除(包括配置和用户)

```bash
sudo apt-get purge quantclaw
```

## 依赖关系

### 必需依赖

- libssl3
- libcurl4
- libsqlite3-0
- zlib1g

### 推荐依赖

- nodejs (>= 18) - 用于 Plugin Sidecar

## 故障排查

### 构建失败

```bash
# 检查依赖
dpkg-checkbuilddeps

# 安装缺失的依赖
sudo apt-get build-dep quantclaw
```

### 服务启动失败

```bash
# 查看详细日志
sudo journalctl -u quantclaw -n 50 --no-pager

# 检查配置
sudo -u quantclaw quantclaw config get

# 手动运行(调试模式)
sudo -u quantclaw quantclaw gateway
```

### 权限问题

```bash
# 修复配置目录权限
sudo chown -R quantclaw:quantclaw /home/quantclaw/.quantclaw
sudo chmod 755 /home/quantclaw/.quantclaw
```

## 高级配置

### 自定义端口

编辑 `/home/quantclaw/.quantclaw/quantclaw.json`:

```json
{
  "gateway": {
    "port": 18800,
    "controlUi": {
      "port": 18801
    }
  }
}
```

重启服务:

```bash
sudo systemctl restart quantclaw
```

### 配置 LLM Provider

```bash
# 使用 quantclaw 用户编辑配置
sudo -u quantclaw quantclaw config set providers.openai.apiKey "sk-..."
sudo -u quantclaw quantclaw config set providers.anthropic.apiKey "sk-ant-..."

# 重新加载配置(无需重启)
sudo -u quantclaw quantclaw config reload
```

## 包维护

### 更新版本

1. 更新 `scripts/DOCKER_VERSION`
2. 更新 `debian/changelog`
3. 重新构建: `./scripts/build-deb.sh`

### 签名包(可选)

```bash
# 使用 GPG 签名
dpkg-buildpackage -k<KEY_ID>
```

## 支持的架构

- amd64 (x86_64)
- arm64 (aarch64)

## 许可证

Apache License 2.0

## 更多信息

- 官方文档: https://quantclaw.github.io
- GitHub: https://github.com/QuantClaw/QuantClaw
- 问题反馈: https://github.com/QuantClaw/QuantClaw/issues
