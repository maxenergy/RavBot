#!/bin/bash
# 测试工具结果截断功能

echo "=== 测试 1: 小输出（正常）==="
./build/ravbot agent "请执行命令: echo 'Hello World'"

echo ""
echo "=== 测试 2: 大输出（应该被截断）==="
./build/ravbot agent "请执行命令: curl -s https://code.claude.com/docs/en/agent-teams | head -c 100000"

echo ""
echo "=== 检查日志中的截断警告 ==="
tail -20 ~/.ravbot/logs/ravbot_$(date +%Y-%m-%d).log | grep -i truncat
