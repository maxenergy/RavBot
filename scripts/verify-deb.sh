#!/bin/bash
# DEB 包验证脚本

set -e

DEB_FILE="$1"

if [ -z "$DEB_FILE" ]; then
    echo "用法: $0 <deb文件路径>"
    exit 1
fi

if [ ! -f "$DEB_FILE" ]; then
    echo "错误: 文件不存在: $DEB_FILE"
    exit 1
fi

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  验证 DEB 包: $(basename $DEB_FILE)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# 1. 检查包信息
echo "📦 包信息:"
dpkg-deb -I "$DEB_FILE" | grep -E "Package:|Version:|Architecture:|Depends:"
echo ""

# 2. 检查文件结构
echo "📁 关键文件检查:"

# 检查主程序
if dpkg -c "$DEB_FILE" | grep -q "./usr/bin/quantclaw$"; then
    echo "  ✅ 主程序: /usr/bin/quantclaw"
else
    echo "  ❌ 主程序缺失"
    exit 1
fi

# 检查 systemd 服务
if dpkg -c "$DEB_FILE" | grep -q "./usr/lib/systemd/system/quantclaw.service$"; then
    echo "  ✅ Systemd 服务: /usr/lib/systemd/system/quantclaw.service"
elif dpkg -c "$DEB_FILE" | grep -q "./lib/systemd/system/quantclaw.service$"; then
    echo "  ⚠️  Systemd 服务路径错误: /lib/systemd/system/ (应该是 /usr/lib/systemd/system/)"
    exit 1
else
    echo "  ❌ Systemd 服务缺失"
    exit 1
fi

# 检查 Sidecar
if dpkg -c "$DEB_FILE" | grep -q "./usr/share/quantclaw/sidecar/dist/index.js$"; then
    echo "  ✅ Sidecar: /usr/share/quantclaw/sidecar/dist/index.js"
else
    echo "  ❌ Sidecar 缺失"
    exit 1
fi

# 检查 node_modules
NODE_MODULES_COUNT=$(dpkg -c "$DEB_FILE" | grep -c "node_modules" || true)
if [ "$NODE_MODULES_COUNT" -gt 0 ]; then
    echo "  ✅ Node.js 依赖: $NODE_MODULES_COUNT 个文件"
else
    echo "  ❌ Node.js 依赖缺失"
    exit 1
fi

# 检查技能
SKILLS_COUNT=$(dpkg -c "$DEB_FILE" | grep -c "/usr/share/quantclaw/skills/" || true)
if [ "$SKILLS_COUNT" -gt 0 ]; then
    echo "  ✅ 内置技能: $SKILLS_COUNT 个文件"
else
    echo "  ⚠️  内置技能缺失"
fi

echo ""

# 3. 检查依赖
echo "📋 依赖检查:"
DEPENDS=$(dpkg-deb -I "$DEB_FILE" | grep "^ Depends:" | sed 's/^ Depends://')

if echo "$DEPENDS" | grep -q "nodejs"; then
    echo "  ✅ nodejs 依赖已声明"
else
    echo "  ❌ nodejs 依赖缺失"
    exit 1
fi

if echo "$DEPENDS" | grep -q "libssl"; then
    echo "  ✅ libssl 依赖已声明"
else
    echo "  ❌ libssl 依赖缺失"
    exit 1
fi

if echo "$DEPENDS" | grep -q "libcurl"; then
    echo "  ✅ libcurl 依赖已声明"
else
    echo "  ❌ libcurl 依赖缺失"
    exit 1
fi

echo ""

# 4. 包大小
SIZE=$(du -h "$DEB_FILE" | cut -f1)
echo "📊 包大小: $SIZE"
echo ""

# 5. SHA256
echo "🔐 SHA256:"
sha256sum "$DEB_FILE"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  ✅ 验证通过!"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "安装命令:"
echo "  sudo dpkg -i $DEB_FILE"
echo "  sudo apt-get install -f"
echo ""
