# Installation Guide

Detailed installation instructions for different platforms and use cases.

## System Requirements

### Minimum Requirements
- **CPU**: 2 cores, 2+ GHz
- **RAM**: 4 GB
- **Disk**: 2 GB free space
- **OS**: Linux (Ubuntu 20.04+), Windows (WSL2), or macOS (10.15+)

### Recommended Requirements
- **CPU**: 4+ cores
- **RAM**: 8+ GB
- **Disk**: 10 GB free space
- **GPU**: Optional (for faster inference if supported by your LLM provider)

## Linux Installation

### Ubuntu/Debian

#### Using Pre-built Binary
```bash
# Download the latest binary
wget https://github.com/RavBot/RavBot/releases/download/v1.0.0/ravbot-linux-x64.tar.gz

# Extract
tar xzf ravbot-linux-x64.tar.gz

# Install to system path
sudo mv ravbot /usr/local/bin/
chmod +x /usr/local/bin/ravbot

# Verify
ravbot --version
```

#### Build from Source
```bash
# Install build dependencies
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  git \
  libssl-dev \
  nlohmann-json3-dev \
  libspdlog-dev

# Clone repository
git clone https://github.com/RavBot/RavBot.git
cd ravbot

# Build
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)

# Optional: Install system-wide
sudo cmake --install .

# Run tests
./ravbot_tests
```

#### Using Docker
```bash
# Pull image
docker pull ravbot:latest

# Run container
docker run -d \
  --name ravbot \
  -p 18800:18800 \
  -p 18801:18801 \
  -v ravbot_data:/home/ravbot/.ravbot \
  ravbot:latest

# View logs
docker logs ravbot
```

### Fedora/CentOS/RHEL

```bash
# Install dependencies
sudo dnf groupinstall "Development Tools" -y
sudo dnf install cmake openssl-devel nlohmann_json-devel spdlog-devel -y

# Build from source
git clone https://github.com/RavBot/RavBot.git
cd ravbot
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
sudo cmake --install .
```

### Arch Linux

```bash
# Install from AUR (if available)
yay -S ravbot

# Or build from source
git clone https://github.com/RavBot/RavBot.git
cd ravbot
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

## Windows Installation

### Windows 10/11 with WSL2

#### Setup WSL2
```powershell
# Enable WSL2
wsl --install

# Or if already installed
wsl --set-default-version 2
```

#### Install in WSL2
Follow the Ubuntu/Linux instructions above within WSL2:
```bash
wsl
cd ~
git clone https://github.com/RavBot/RavBot.git
# ... follow Linux build steps
```

#### Run from Windows
```powershell
# Forward WSL2 port to Windows
# In WSL2 terminal:
ravbot gateway

# In PowerShell (Windows):
# open http://localhost:18801
```

### Native Windows (MSVC)

#### Prerequisites
- **Visual Studio 2019+** or **Build Tools for Visual Studio**
- **CMake 3.15+**
- **Git**

#### Build Process
```batch
REM Clone repository
git clone https://github.com/RavBot/RavBot.git
cd ravbot

REM Create build directory
mkdir build
cd build

REM Generate Visual Studio project
cmake .. -G "Visual Studio 17 2022"

REM Build
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%

REM Tests
Release\ravbot_tests.exe
```

#### Install Dependencies (vcpkg)
```batch
REM If CMake can't find dependencies
vcpkg install nlohmann-json openssl spdlog zlib

REM Then configure with toolchain
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
```

### Docker on Windows
```powershell
# Pull and run Docker container
docker pull ravbot:latest

docker run -d `
  --name ravbot `
  -p 18800:18800 `
  -p 18801:18801 `
  -v ravbot_data:/home/ravbot/.ravbot `
  ravbot:latest

# View logs
docker logs ravbot

# Stop container
docker stop ravbot
```

## macOS Installation

### Using Homebrew (if tap available)
```bash
brew tap ravbot/ravbot
brew install ravbot
```

### Build from Source

#### Install Dependencies
```bash
# Using Homebrew
brew install cmake openssl nlohmann-json spdlog

# Or using MacPorts
sudo port install cmake openssl nlohmann_json spdlog
```

#### Build
```bash
git clone https://github.com/RavBot/RavBot.git
cd ravbot

mkdir build && cd build
cmake -DOPENSSL_DIR=$(brew --prefix openssl) ..
cmake --build . -j $(sysctl -n hw.ncpu)

# Run tests
./ravbot_tests

# Install
sudo cmake --install .
```

## Post-Installation Setup

### Initialize Configuration

```bash
# Interactive setup (recommended)
ravbot onboard

# Quick setup (use defaults)
ravbot onboard --quick
```

During setup, you'll configure:
- **API Keys**: LLM provider credentials
- **Default Model**: Primary LLM model
- **Workspace**: Location for files and memory
- **Plugins**: Initial plugin selection

### Verify Installation

```bash
# Check version
ravbot --version

# Run tests
ravbot status

# Test basic functionality
ravbot run "Hello, what's your name?"
```

### Configure Environment (Optional)

```bash
# Set default agent
export RAVBOT_AGENT_ID=main

# Set configuration directory
export RAVBOT_CONFIG_DIR=~/.ravbot

# Set log level
export RAVBOT_LOG_LEVEL=debug

# Set gateway port
export RAVBOT_GATEWAY_PORT=18800
```

## Updating RavBot

### From Binary
```bash
# Download new version
wget https://github.com/RavBot/RavBot/releases/download/v1.1.0/ravbot-linux-x64.tar.gz

# Backup old binary
cp /usr/local/bin/ravbot /usr/local/bin/ravbot.backup

# Install new version
tar xzf ravbot-linux-x64.tar.gz
sudo mv ravbot /usr/local/bin/

# Verify
ravbot --version
```

### From Source
```bash
cd ravbot
git pull origin main
cd build
cmake --build . -j$(nproc)
sudo cmake --install .
```

### From Homebrew
```bash
brew update
brew upgrade ravbot
```

### Docker
```bash
docker pull ravbot:latest
docker stop ravbot
docker rm ravbot

# Re-run with new image
docker run -d \
  --name ravbot \
  -p 18800:18800 \
  -p 18801:18801 \
  -v ravbot_data:/home/ravbot/.ravbot \
  ravbot:latest
```

## Troubleshooting

### Build Failures

**CMake not found**
```bash
# Ubuntu
sudo apt-get install cmake

# macOS
brew install cmake

# Windows
choco install cmake
```

**Missing dependencies**
```bash
# Ubuntu
sudo apt-get install libssl-dev nlohmann-json3-dev libspdlog-dev

# macOS
brew install openssl nlohmann-json spdlog
```

**C++ compiler issues**
```bash
# Update compiler
sudo apt-get install build-essential  # Ubuntu/Debian
brew install gcc                       # macOS
```

### Runtime Issues

**Port already in use**
```bash
# Change gateway port
ravbot gateway --port 9000

# Or find and kill process using port 18800
lsof -i :18800
kill -9 <PID>
```

**Permission denied errors**
```bash
# Ensure ~/.ravbot is writable
chmod 700 ~/.ravbot
chmod 600 ~/.ravbot/*
```

**Configuration issues**
```bash
# Validate configuration
ravbot config validate

# View schema
ravbot config schema

# Reset to defaults
ravbot onboard --reset
```

## Uninstallation

### Binary Installation
```bash
# Remove binary
sudo rm /usr/local/bin/ravbot

# Remove configuration (optional)
rm -rf ~/.ravbot
```

### Docker
```bash
docker stop ravbot
docker rm ravbot
docker rmi ravbot:latest
```

### From Source
```bash
cd ravbot/build
sudo cmake --uninstall

# Or manually
sudo rm /usr/local/bin/ravbot
sudo rm -rf /usr/local/include/ravbot
```

---

**Next**: [Configure your installation](/guide/configuration) or [get started running agents](/guide/getting-started).
