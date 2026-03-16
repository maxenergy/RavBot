# 传输 DEB 包到测试机器

## 快速传输

### 方法 1: SCP (推荐)

```bash
# 从构建机器传输到测试机器
scp dist/quantclaw_0.3.0-1_amd64.deb kaifa@kaifa:~/下载/

# 或者指定 IP
scp dist/quantclaw_0.3.0-1_amd64.deb kaifa@192.168.x.x:~/下载/
```

### 方法 2: HTTP 服务器

**在构建机器上:**
```bash
# 启动简单 HTTP 服务器
cd dist
python3 -m http.server 8000
# 或
npx http-server -p 8000
```

**在测试机器上:**
```bash
# 下载包
wget http://构建机器IP:8000/quantclaw_0.3.0-1_amd64.deb
# 或
curl -O http://构建机器IP:8000/quantclaw_0.3.0-1_amd64.deb
```

### 方法 3: USB 驱动器

```bash
# 复制到 USB
cp dist/quantclaw_0.3.0-1_amd64.deb /media/usb/

# 在测试机器上
cp /media/usb/quantclaw_0.3.0-1_amd64.deb ~/下载/
```

### 方法 4: 共享文件夹 (虚拟机)

如果测试机器是虚拟机:
```bash
# VirtualBox 共享文件夹
cp dist/quantclaw_0.3.0-1_amd64.deb /path/to/shared/folder/

# 在虚拟机中访问
cp /media/sf_shared/quantclaw_0.3.0-1_amd64.deb ~/下载/
```

## 验证传输

```bash
# 在测试机器上验证文件完整性
sha256sum ~/下载/quantclaw_0.3.0-1_amd64.deb

# 应该输出:
# b3dadb6dc25e7fce177dcf150fe36be9d11470bd20256a8660ca5ea07e4dfbbb
```

## 安装

```bash
cd ~/下载
sudo dpkg -i quantclaw_0.3.0-1_amd64.deb
sudo apt-get install -f
```

## 完整测试流程

参考: [docs/INSTALL_TEST_GUIDE.md](docs/INSTALL_TEST_GUIDE.md)
