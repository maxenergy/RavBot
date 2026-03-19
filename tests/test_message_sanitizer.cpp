// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/gateway/message_sanitizer.hpp"

#include <gtest/gtest.h>

using namespace ravbot;

class MessageSanitizerTest : public ::testing::Test {
 protected:
  MessageSanitizer sanitizer_;
};

// 测试移除 <system.run> 标签
TEST_F(MessageSanitizerTest, RemoveSystemRunTags) {
  std::string input =
      "执行命令: <system.run><command>df -h</command></system.run> 完成";
  std::string expected = "执行命令:  完成";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <command> 标签
TEST_F(MessageSanitizerTest, RemoveCommandTags) {
  std::string input = "运行 <command>uptime</command> 查看系统运行时间";
  std::string expected = "运行  查看系统运行时间";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <thinking> 标签
TEST_F(MessageSanitizerTest, RemoveThinkingTags) {
  std::string input = "让我思考一下 <thinking>这是内部思考</thinking> 好的";
  std::string expected = "让我思考一下  好的";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <system> 标签
TEST_F(MessageSanitizerTest, RemoveSystemTags) {
  std::string input = "系统消息: <system>内部系统消息</system> 结束";
  std::string expected = "系统消息:  结束";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <internal> 标签
TEST_F(MessageSanitizerTest, RemoveInternalTags) {
  std::string input = "处理中 <internal>内部处理信息</internal> 完成";
  std::string expected = "处理中  完成";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <debug> 标签
TEST_F(MessageSanitizerTest, RemoveDebugTags) {
  std::string input = "调试信息: <debug>调试输出</debug> 继续";
  std::string expected = "调试信息:  继续";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <tool_use> 标签
TEST_F(MessageSanitizerTest, RemoveToolUseTags) {
  std::string input = "使用工具 <tool_use>bash</tool_use> 执行";
  std::string expected = "使用工具  执行";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <tool_result> 标签
TEST_F(MessageSanitizerTest, RemoveToolResultTags) {
  std::string input = "结果: <tool_result>工具执行结果</tool_result> 完成";
  std::string expected = "结果:  完成";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除多个标签
TEST_F(MessageSanitizerTest, RemoveMultipleTags) {
  std::string input =
      "开始 <system.run><command>ls</command></system.run> 和 "
      "<thinking>思考</thinking> 结束";
  std::string expected = "开始  和  结束";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试嵌套标签
TEST_F(MessageSanitizerTest, RemoveNestedTags) {
  std::string input =
      "外层 <system>内层 <command>嵌套命令</command> 继续</system> 结束";
  std::string expected = "外层  结束";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试不完整的标签（只有开始标签）
// 注意：不完整的标签不会被移除，这是预期行为
TEST_F(MessageSanitizerTest, RemoveIncompleteOpenTags) {
  std::string input = "开始 <system.run> 继续文本";
  // 不完整的标签不会被移除
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, input);
}

// 测试大小写不敏感
TEST_F(MessageSanitizerTest, CaseInsensitive) {
  std::string input =
      "测试 <SYSTEM.RUN>大写</SYSTEM.RUN> 和 <System>混合</System> 结束";
  std::string expected = "测试  和  结束";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试保留正常文本
TEST_F(MessageSanitizerTest, PreserveNormalText) {
  std::string input = "这是正常的文本，没有任何系统标签";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, input);
}

// 测试保留边界标记清理功能
TEST_F(MessageSanitizerTest, RemoveBoundaryMarkers) {
  std::string input = "文本 ⟨⟨EXTERNAL_CONTENT⟩⟩内容⟨⟨/EXTERNAL_CONTENT⟩⟩ 结束";
  std::string expected = "文本 内容 结束";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试同时移除系统标签和边界标记
TEST_F(MessageSanitizerTest, RemoveBothSystemTagsAndBoundaryMarkers) {
  std::string input =
      "开始 <system.run>命令</system.run> 和 ⟨⟨UNTRUSTED⟩⟩内容⟨⟨/UNTRUSTED⟩⟩ "
      "结束";
  std::string expected = "开始  和 内容 结束";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试真实场景：Telegram 对话中的系统标签
TEST_F(MessageSanitizerTest, RealWorldTelegramScenario) {
  std::string input = R"(好的，我来检查系统健康状态。

<system.run><command>df -h</command></system.run>

系统磁盘使用情况正常。)";

  std::string expected = R"(好的，我来检查系统健康状态。



系统磁盘使用情况正常。)";

  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试空字符串
TEST_F(MessageSanitizerTest, EmptyString) {
  std::string input = "";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, "");
}

// 测试只有标签的字符串
TEST_F(MessageSanitizerTest, OnlyTags) {
  std::string input = "<system.run><command>test</command></system.run>";
  std::string expected = "";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <function_calls> 标签
TEST_F(MessageSanitizerTest, RemoveFunctionCallsTags) {
  std::string input = R"(执行操作 <function_calls>
  <invoke name="Read">
    <parameter name="file_path">/tmp/test.txt</parameter>
  </invoke>
</function_calls> 完成)";
  std::string expected = "执行操作  完成";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <invoke> 标签
TEST_F(MessageSanitizerTest, RemoveInvokeTags) {
  std::string input = "调用工具 <invoke name=\"Bash\">内容</invoke> 结束";
  std::string expected = "调用工具  结束";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <parameter> 标签
TEST_F(MessageSanitizerTest, RemoveParameterTags) {
  std::string input = "参数 <parameter name=\"test\">value</parameter> 结束";
  std::string expected = "参数  结束";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 <system-reminder> 标签
TEST_F(MessageSanitizerTest, RemoveSystemReminderTags) {
  std::string input =
      "消息 <system-reminder>系统提醒内容</system-reminder> 继续";
  std::string expected = "消息  继续";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除 Bash 命令输出
TEST_F(MessageSanitizerTest, RemoveBashCommands) {
  std::string input = R"(检查文件
$ ls -la
total 100
# 这是注释
继续文本)";
  std::string expected = R"(检查文件

total 100

继续文本)";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除文件路径和行号
TEST_F(MessageSanitizerTest, RemoveFilePathsWithLineNumbers) {
  std::string input = "错误在 main.cpp:123 和 test.hpp:456 位置";
  std::string expected = "错误在  和  位置";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

// 测试移除工具执行标记
TEST_F(MessageSanitizerTest, RemoveToolExecutionMarkers) {
  std::string input = "Tool: Read\nExecuting: cat file.txt\n结果显示";
  std::string expected = "\n\n结果显示";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

TEST_F(MessageSanitizerTest, RemovePseudoToolCallMarkup) {
  std::string input = R"(开始
<tool_call>
<function=exec>
<parameter=command>
ls -la /tmp
</parameter>
</function>
结束)";
  std::string expected = "开始\n\n结束";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

TEST_F(MessageSanitizerTest, RemovePlainExecMarkup) {
  std::string input = R"(让我先查看一下工作区的完成报告，了解当前的进度状态：

<exec>
<parameter=command>
cat /tmp/report.md
</parameter>
</exec>)";
  std::string expected =
      "让我先查看一下工作区的完成报告，了解当前的进度状态：\n\n";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, expected);
}

TEST_F(MessageSanitizerTest, PreserveInlineLiteralToolTags) {
  std::string input = u8"请解释 `<exec>` 和 `<tool_call>` 的区别。";
  std::string result = sanitizer_.SanitizeOutput(input);
  EXPECT_EQ(result, input);
}

// 测试综合场景：移除所有系统标签
TEST_F(MessageSanitizerTest, ComprehensiveSystemTagRemoval) {
  std::string input = R"(用户消息
<function_calls>
  <invoke name="Bash">
    <parameter name="command">ls -la</parameter>
  </invoke>
</function_calls>
$ cd /tmp
Tool: Read
Executing: cat file.txt
错误在 main.cpp:100
<system-reminder>系统提醒</system-reminder>
最终消息)";

  std::string result = sanitizer_.SanitizeOutput(input);

  // 验证所有系统标签都被移除
  EXPECT_EQ(result.find("<function_calls>"), std::string::npos);
  EXPECT_EQ(result.find("<invoke>"), std::string::npos);
  EXPECT_EQ(result.find("<parameter>"), std::string::npos);
  EXPECT_EQ(result.find("$ cd /tmp"), std::string::npos);
  EXPECT_EQ(result.find("Tool: Read"), std::string::npos);
  EXPECT_EQ(result.find("Executing:"), std::string::npos);
  EXPECT_EQ(result.find("main.cpp:100"), std::string::npos);
  EXPECT_EQ(result.find("<system-reminder>"), std::string::npos);

  // 验证用户消息保留
  EXPECT_NE(result.find("用户消息"), std::string::npos);
  EXPECT_NE(result.find("最终消息"), std::string::npos);
}
