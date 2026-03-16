# ToDesk 连接卡在 100% 问题解决方案

**报告时间:** 2026-03-16 19:16
**远程机器:** kaifa@192.168.1.104
**问题:** ToDesk 连接卡在 100%

## 问题诊断

### 服务状态
- ✅ ToDesk 服务正在运行 (todeskd.service)
- ✅ ToDesk 进程正常
- ⚠️ 系统使用 Wayland 显示服务器

### 根本原因
ToDesk 在 Ubuntu 24.04 的 Wayland 环境下可能存在兼容性问题，导致连接卡在 100%。

## 已执行的修复

### 1. 禁用 Wayland
```bash
sudo sed -i 's/#WaylandEnable=false/WaylandEnable=false/' /etc/gdm3/custom.conf
```

这将强制系统使用 X11 而不是 Wayland。

### 2. 重启 ToDesk 服务
```bash
sudo systemctl restart todeskd
```

## 需要执行的操作

### 方案 A: 重启系统（推荐）

**最简单有效的方法：**
```bash
ssh kaifa@192.168.1.104
sudo reboot
```

重启后系统将使用 X11，ToDesk 应该能正常连接。

### 方案 B: 重新登录（不重启系统）

1. **注销当前用户会话**
   ```bash
   ssh kaifa@192.168.1.104
   # 注销图形界面用户
   pkill -u kaifa gnome-session
   ```

2. **在物理机器上重新登录**
   - 在登录界面，点击右下角齿轮图标
   - 选择 "Ubuntu on Xorg" 而不是默认的 "Ubuntu"
   - 输入密码登录

3. **验证显示服务器**
   ```bash
   echo $XDG_SESSION_TYPE
   # 应该输出: x11
   ```

### 方案 C: 手动配置虚拟显示（高级）

如果上述方案不可行，可以配置 ToDesk 使用虚拟显示：

```bash
# 1. 安装 xvfb
sudo apt-get install xvfb

# 2. 创建虚拟显示
Xvfb :99 -screen 0 1920x1080x24 &

# 3. 配置 ToDesk 使用虚拟显示
export DISPLAY=:99
sudo systemctl restart todeskd
```

## 其他可能的解决方案

### 1. 检查防火墙
```bash
# 检查防火墙状态
sudo ufw status

# 如果启用了防火墙，允许 ToDesk 端口
sudo ufw allow 50000:50100/tcp
sudo ufw allow 50000:50100/udp
```

### 2. 检查网络代理
如果系统配置了代理，可能影响 ToDesk 连接：
```bash
# 临时禁用代理
unset http_proxy
unset https_proxy
unset HTTP_PROXY
unset HTTPS_PROXY

# 重启 ToDesk
sudo systemctl restart todeskd
```

### 3. 重新安装 ToDesk
```bash
# 完全卸载
sudo systemctl stop todeskd
sudo apt-get remove --purge todesk
sudo rm -rf /opt/todesk
sudo rm -rf ~/.todesk

# 重新下载安装
wget https://dl.todesk.com/linux/todesk-v4.x.x-amd64.deb
sudo dpkg -i todesk-v4.x.x-amd64.deb
sudo apt-get install -f
```

### 4. 检查 ToDesk 设备码
```bash
# 获取设备码
todesk --device

# 或
/opt/todesk/bin/todesk --device
```

确保设备码正确，并在客户端使用正确的设备码连接。

## 验证修复

### 1. 检查显示服务器类型
```bash
ssh kaifa@192.168.1.104
echo $XDG_SESSION_TYPE
# 应该输出: x11 (不是 wayland)
```

### 2. 检查 ToDesk 服务状态
```bash
systemctl status todeskd
# 应该显示: active (running)
```

### 3. 检查 ToDesk 进程
```bash
ps aux | grep -i todesk | grep -v grep
# 应该看到 ToDesk_Service 和 ToDesk_Session 进程
```

### 4. 尝试连接
在客户端使用 ToDesk 连接到远程机器，应该能够正常连接。

## 常见问题排查

### Q1: 重启后仍然卡在 100%
**A:** 检查是否真的切换到了 X11：
```bash
echo $XDG_SESSION_TYPE
```
如果仍然是 wayland，手动在登录界面选择 "Ubuntu on Xorg"。

### Q2: 找不到 "Ubuntu on Xorg" 选项
**A:** 安装 X11 会话：
```bash
sudo apt-get install xserver-xorg-core
sudo apt-get install ubuntu-session
```

### Q3: 连接后黑屏
**A:** 可能是显示驱动问题：
```bash
# 检查显卡驱动
lspci | grep VGA
ubuntu-drivers devices

# 安装推荐驱动
sudo ubuntu-drivers autoinstall
```

### Q4: 连接后鼠标键盘无响应
**A:** 权限问题：
```bash
# 将用户添加到 input 组
sudo usermod -aG input kaifa

# 重启 ToDesk
sudo systemctl restart todeskd
```

## 推荐操作流程

**最简单的解决方案（推荐）：**

1. **重启远程机器**
   ```bash
   ssh kaifa@192.168.1.104
   sudo reboot
   ```

2. **等待系统启动（约 1-2 分钟）**

3. **尝试 ToDesk 连接**
   - 打开 ToDesk 客户端
   - 输入设备码
   - 连接应该能正常建立

4. **如果仍然失败**
   - 在物理机器上登录
   - 在登录界面选择 "Ubuntu on Xorg"
   - 重新尝试连接

## 技术说明

### Wayland vs X11
- **Wayland**: Ubuntu 24.04 默认显示服务器，更现代但兼容性问题较多
- **X11**: 传统显示服务器，兼容性更好，ToDesk 支持更完善

### ToDesk 兼容性
- ToDesk 在 X11 环境下工作最稳定
- Wayland 环境可能导致：
  - 连接卡在 100%
  - 黑屏
  - 鼠标键盘无响应
  - 性能问题

## 相关文件

- **配置文件:** `/etc/gdm3/custom.conf`
- **服务文件:** `/etc/systemd/system/todeskd.service`
- **日志文件:** `/opt/todesk/log/todesk.log`
- **用户配置:** `~/.todesk/`

## 联系支持

如果以上方案都无法解决问题：
- ToDesk 官方支持: https://www.todesk.com/support
- ToDesk 论坛: https://bbs.todesk.com

---

**当前状态:** ⏳ 等待重启系统
**下一步:** 重启远程机器并测试连接
