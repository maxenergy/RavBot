#!/bin/bash
# QuantClaw 综合功能测试脚本
# 专注于上下文管理、对话递归传递和工具续轮

set -e

echo "=========================================="
echo "QuantClaw 综合功能测试"
echo "=========================================="
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 检查构建目录
if [ ! -d "build" ]; then
    echo -e "${YELLOW}构建目录不存在，正在创建...${NC}"
    mkdir -p build
fi

cd build

# 配置和构建
echo -e "${BLUE}[1/4] 配置 CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON

echo ""
echo -e "${BLUE}[2/4] 编译测试...${NC}"
cmake --build . --target quantclaw_tests -j$(nproc)

echo ""
echo -e "${BLUE}[3/4] 运行核心上下文和对话测试...${NC}"
echo ""

# 定义测试类别
declare -a CONTEXT_TESTS=(
    "ComprehensiveContextTest.*"
    "AgentLoopTest.AnthropicReplayKeepsOnlyCurrentUserTurnDuringToolResultFollowUp"
    "AgentLoopTest.AnthropicReplayNarrowsToolsToMatchedToolNames"
    "AgentLoopTest.AnthropicSanitizesDanglingToolUseAndMergesUserTurns"
    "AgentLoopTest.AnthropicCollapsesCompletedHistoricalToolTurnsBeforeFollowUp"
    "ContextPrunerTest.*"
    "SessionManagerTest.*"
)

declare -a LOGIC_TESTS=(
    "AgentLoopTest.ProcessMessageWithHistory"
    "AgentLoopTest.StreamingCallback"
    "AgentLoopTest.UsesConfig*"
    "ToolChainTest.*"
    "PromptBuilderTest.*"
)

# 运行上下文相关测试
echo -e "${GREEN}=== 上下文管理和工具续轮测试 ===${NC}"
for test in "${CONTEXT_TESTS[@]}"; do
    echo -e "${YELLOW}运行: $test${NC}"
    ./quantclaw_tests --gtest_filter="$test" --gtest_color=yes || {
        echo -e "${RED}测试失败: $test${NC}"
        exit 1
    }
done

echo ""
echo -e "${GREEN}=== 逻辑处理和递归传递测试 ===${NC}"
for test in "${LOGIC_TESTS[@]}"; do
    echo -e "${YELLOW}运行: $test${NC}"
    ./quantclaw_tests --gtest_filter="$test" --gtest_color=yes || {
        echo -e "${RED}测试失败: $test${NC}"
        exit 1
    }
done

echo ""
echo -e "${BLUE}[4/4] 运行完整测试套件...${NC}"
./quantclaw_tests --gtest_color=yes

echo ""
echo -e "${GREEN}=========================================="
echo "所有测试通过！✓"
echo "==========================================${NC}"

