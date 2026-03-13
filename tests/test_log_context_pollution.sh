#!/bin/bash
# Copyright 2025 QuantClaw Contributors
# SPDX-License-Identifier: Apache-2.0
#
# 自动化日志抓取和上下文污染检测脚本

set -euo pipefail

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
LOG_DIR="$BUILD_DIR/test_logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
TEST_LOG="$LOG_DIR/context_pollution_test_${TIMESTAMP}.log"

# 创建日志目录
mkdir -p "$LOG_DIR"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}QuantClaw 上下文污染自动化检测${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "项目根目录: $PROJECT_ROOT"
echo "构建目录: $BUILD_DIR"
echo "日志目录: $LOG_DIR"
echo "测试日志: $TEST_LOG"
echo ""

# 步骤 1: 编译项目
echo -e "${YELLOW}[1/5] 编译项目...${NC}"
cd "$BUILD_DIR"
if cmake --build . --target quantclaw_tests -j$(nproc) 2>&1 | tee -a "$TEST_LOG"; then
    echo -e "${GREEN}✓ 编译成功${NC}"
else
    echo -e "${RED}✗ 编译失败${NC}"
    exit 1
fi
echo ""

# 步骤 2: 运行上下文污染测试
echo -e "${YELLOW}[2/5] 运行上下文污染测试...${NC}"
if ./quantclaw_tests --gtest_filter="ContextPollutionTest.*" --gtest_color=yes 2>&1 | tee -a "$TEST_LOG"; then
    echo -e "${GREEN}✓ 上下文污染测试通过${NC}"
else
    echo -e "${RED}✗ 上下文污染测试失败${NC}"
    exit 1
fi
echo ""

# 步骤 3: 运行综合上下文测试
echo -e "${YELLOW}[3/5] 运行综合上下文测试...${NC}"
if ./quantclaw_tests --gtest_filter="ComprehensiveContextTest.*" --gtest_color=yes 2>&1 | tee -a "$TEST_LOG"; then
    echo -e "${GREEN}✓ 综合上下文测试通过${NC}"
else
    echo -e "${RED}✗ 综合上下文测试失败${NC}"
    exit 1
fi
echo ""

# 步骤 4: 运行 Agent Loop 测试
echo -e "${YELLOW}[4/5] 运行 Agent Loop 测试...${NC}"
if ./quantclaw_tests --gtest_filter="AgentLoopTest.AnthropicReplay*" --gtest_color=yes 2>&1 | tee -a "$TEST_LOG"; then
    echo -e "${GREEN}✓ Agent Loop 测试通过${NC}"
else
    echo -e "${RED}✗ Agent Loop 测试失败${NC}"
    exit 1
fi
echo ""

# 步骤 5: 分析日志
echo -e "${YELLOW}[5/5] 分析测试日志...${NC}"

# 检查是否有上下文裁剪日志
CONTEXT_TRIMMED=$(grep -c "Context trimmed" "$TEST_LOG" || true)
echo "  - 上下文裁剪次数: $CONTEXT_TRIMMED"

# 检查是否有工具执行日志
TOOL_EXECUTIONS=$(grep -c "Executing tool" "$TEST_LOG" || true)
echo "  - 工具执行次数: $TOOL_EXECUTIONS"

# 检查是否有错误
ERRORS=$(grep -c "ERROR\|FAILED\|error:" "$TEST_LOG" || true)
if [ "$ERRORS" -gt 0 ]; then
    echo -e "  - ${RED}发现 $ERRORS 个错误${NC}"
else
    echo -e "  - ${GREEN}未发现错误${NC}"
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}测试完成！${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "详细日志: $TEST_LOG"
echo ""

# 生成测试报告
REPORT_FILE="$LOG_DIR/context_pollution_report_${TIMESTAMP}.md"
cat > "$REPORT_FILE" << EOF
# QuantClaw 上下文污染测试报告

**测试时间**: $(date)
**测试日志**: $TEST_LOG

## 测试结果

### 1. 上下文污染测试
- 状态: ✓ 通过
- 测试用例: ContextPollutionTest.*

### 2. 综合上下文测试
- 状态: ✓ 通过
- 测试用例: ComprehensiveContextTest.*

### 3. Agent Loop 测试
- 状态: ✓ 通过
- 测试用例: AgentLoopTest.AnthropicReplay*

## 日志分析

- 上下文裁剪次数: $CONTEXT_TRIMMED
- 工具执行次数: $TOOL_EXECUTIONS
- 错误数量: $ERRORS

## 结论

所有测试通过，上下文污染问题已修复。

EOF

echo "测试报告: $REPORT_FILE"
echo ""

exit 0

