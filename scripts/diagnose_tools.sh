#!/bin/bash
# QuantClaw 工具权限诊断和修复脚本

CONFIG_FILE="$HOME/.quantclaw/quantclaw.json"

echo "=========================================="
echo "QuantClaw 工具权限诊断"
echo "=========================================="
echo ""

# 检查配置文件
if [ ! -f "$CONFIG_FILE" ]; then
    echo "❌ 配置文件不存在: $CONFIG_FILE"
    exit 1
fi

echo "✅ 配置文件存在: $CONFIG_FILE"
echo ""

# 检查当前工具配置
echo "当前工具配置:"
echo "----------------------------------------"
jq '.tools' "$CONFIG_FILE"
echo ""

# 检查 allow 列表
ALLOW_LIST=$(jq -r '.tools.allow | length' "$CONFIG_FILE")
echo "工具白名单数量: $ALLOW_LIST"

if [ "$ALLOW_LIST" -eq 0 ]; then
    echo "⚠️  警告: 工具白名单为空,这会阻止所有工具执行!"
    echo ""
    echo "建议修复方案:"
    echo "1. 允许所有工具 (推荐用于开发环境)"
    echo "2. 允许特定工具 (推荐用于生产环境)"
    echo "3. 保持当前配置 (最严格)"
    echo ""

    read -p "是否修复? (1/2/n): " choice

    case $choice in
        1)
            echo "修复: 允许所有工具..."
            # 备份配置
            cp "$CONFIG_FILE" "$CONFIG_FILE.backup.$(date +%Y%m%d_%H%M%S)"

            # 设置 allow 为 ["*"]
            jq '.tools.allow = ["*"]' "$CONFIG_FILE" > "$CONFIG_FILE.tmp"
            mv "$CONFIG_FILE.tmp" "$CONFIG_FILE"

            echo "✅ 已修复: 允许所有工具"
            echo "备份文件: $CONFIG_FILE.backup.*"
            ;;
        2)
            echo "修复: 允许常用诊断工具..."
            # 备份配置
            cp "$CONFIG_FILE" "$CONFIG_FILE.backup.$(date +%Y%m%d_%H%M%S)"

            # 设置常用工具白名单
            jq '.tools.allow = ["bash", "exec", "read_file", "write_file", "list_dir", "search_files", "web_search"]' "$CONFIG_FILE" > "$CONFIG_FILE.tmp"
            mv "$CONFIG_FILE.tmp" "$CONFIG_FILE"

            echo "✅ 已修复: 允许常用工具"
            echo "允许的工具: bash, exec, read_file, write_file, list_dir, search_files, web_search"
            echo "备份文件: $CONFIG_FILE.backup.*"
            ;;
        *)
            echo "跳过修复"
            ;;
    esac
else
    echo "✅ 工具白名单已配置"
    echo "允许的工具:"
    jq -r '.tools.allow[]' "$CONFIG_FILE"
fi

echo ""
echo "=========================================="
echo "诊断完成"
echo "=========================================="
echo ""

# 显示当前配置
echo "当前完整工具配置:"
jq '.tools' "$CONFIG_FILE"
echo ""

# 测试建议
echo "测试建议:"
echo "1. 重启 QuantClaw: systemctl --user restart quantclaw"
echo "2. 测试命令: 在 Telegram 中发送 '请执行 uptime'"
echo "3. 查看日志: journalctl --user -u quantclaw -f"
