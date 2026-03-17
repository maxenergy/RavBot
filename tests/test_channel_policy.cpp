// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include "ravbot/channels/channel_policy.hpp"

static std::shared_ptr<spdlog::logger> make_null_logger() {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>("test", null_sink);
}

// --- DmPolicy / DmScope / GroupActivation enums ---

TEST(ChannelPolicyEnumsTest, DmPolicyFromString) {
  EXPECT_EQ(ravbot::DmPolicyFromString("open"), ravbot::DmPolicy::kOpen);
  EXPECT_EQ(ravbot::DmPolicyFromString("pairing"), ravbot::DmPolicy::kPairing);
  EXPECT_EQ(ravbot::DmPolicyFromString("unknown"), ravbot::DmPolicy::kOpen);
}

TEST(ChannelPolicyEnumsTest, DmPolicyToString) {
  EXPECT_EQ(ravbot::DmPolicyToString(ravbot::DmPolicy::kOpen), "open");
  EXPECT_EQ(ravbot::DmPolicyToString(ravbot::DmPolicy::kPairing), "pairing");
}

TEST(ChannelPolicyEnumsTest, DmScopeFromString) {
  EXPECT_EQ(ravbot::DmScopeFromString("main"), ravbot::DmScope::kMain);
  EXPECT_EQ(ravbot::DmScopeFromString("per-peer"), ravbot::DmScope::kPerPeer);
  EXPECT_EQ(ravbot::DmScopeFromString("per-channel-peer"), ravbot::DmScope::kPerChannelPeer);
  EXPECT_EQ(ravbot::DmScopeFromString("per-account-channel-peer"), ravbot::DmScope::kPerAccountChannelPeer);
  EXPECT_EQ(ravbot::DmScopeFromString("garbage"), ravbot::DmScope::kPerChannelPeer);
}

TEST(ChannelPolicyEnumsTest, GroupActivationFromString) {
  EXPECT_EQ(ravbot::GroupActivationFromString("always"), ravbot::GroupActivation::kAlways);
  EXPECT_EQ(ravbot::GroupActivationFromString("mention"), ravbot::GroupActivation::kMention);
  EXPECT_EQ(ravbot::GroupActivationFromString("other"), ravbot::GroupActivation::kMention);
}

// --- ChannelPolicyConfig ---

TEST(ChannelPolicyConfigTest, FromJsonDefaults) {
  nlohmann::json j = nlohmann::json::object();
  auto c = ravbot::ChannelPolicyConfig::FromJson(j);
  EXPECT_EQ(c.dm_policy, ravbot::DmPolicy::kOpen);
  EXPECT_EQ(c.dm_scope, ravbot::DmScope::kPerChannelPeer);
  EXPECT_EQ(c.group_activation, ravbot::GroupActivation::kMention);
  EXPECT_EQ(c.group_chunk_size, 2000);
  EXPECT_TRUE(c.allow_from.empty());
}

TEST(ChannelPolicyConfigTest, FromJsonFull) {
  nlohmann::json j = {
      {"dmPolicy", "pairing"},
      {"dmScope", "per-peer"},
      {"groupActivation", "always"},
      {"groupChunkSize", 3000},
      {"botName", "MyBot"},
      {"allowFrom", {"user1", "user2"}},
  };
  auto c = ravbot::ChannelPolicyConfig::FromJson(j);
  EXPECT_EQ(c.dm_policy, ravbot::DmPolicy::kPairing);
  EXPECT_EQ(c.dm_scope, ravbot::DmScope::kPerPeer);
  EXPECT_EQ(c.group_activation, ravbot::GroupActivation::kAlways);
  EXPECT_EQ(c.group_chunk_size, 3000);
  EXPECT_EQ(c.bot_name, "MyBot");
  ASSERT_EQ(c.allow_from.size(), 2);
  EXPECT_EQ(c.allow_from[0], "user1");
}

// --- PairingManager ---

TEST(PairingManagerTest, GenerateAndVerifyCode) {
  auto logger = make_null_logger();
  ravbot::PairingManager pm(logger);

  auto code = pm.GenerateCode("discord");
  EXPECT_EQ(code.size(), 6);

  EXPECT_FALSE(pm.IsPaired("discord", "user123"));
  EXPECT_TRUE(pm.VerifyCode("discord", code, "user123"));
  EXPECT_TRUE(pm.IsPaired("discord", "user123"));
}

TEST(PairingManagerTest, WrongCodeFails) {
  auto logger = make_null_logger();
  ravbot::PairingManager pm(logger);

  pm.GenerateCode("telegram");
  EXPECT_FALSE(pm.VerifyCode("telegram", "000000", "user1"));
  EXPECT_FALSE(pm.IsPaired("telegram", "user1"));
}

TEST(PairingManagerTest, CodeConsumedAfterUse) {
  auto logger = make_null_logger();
  ravbot::PairingManager pm(logger);

  auto code = pm.GenerateCode("discord");
  EXPECT_TRUE(pm.VerifyCode("discord", code, "user1"));
  // Code should be consumed
  EXPECT_FALSE(pm.VerifyCode("discord", code, "user2"));
}

TEST(PairingManagerTest, Unpair) {
  auto logger = make_null_logger();
  ravbot::PairingManager pm(logger);

  auto code = pm.GenerateCode("discord");
  pm.VerifyCode("discord", code, "user1");
  EXPECT_TRUE(pm.IsPaired("discord", "user1"));

  pm.Unpair("discord", "user1");
  EXPECT_FALSE(pm.IsPaired("discord", "user1"));
}

TEST(PairingManagerTest, PairedSendersList) {
  auto logger = make_null_logger();
  ravbot::PairingManager pm(logger);

  auto code1 = pm.GenerateCode("ch");
  pm.VerifyCode("ch", code1, "a");
  auto code2 = pm.GenerateCode("ch");
  pm.VerifyCode("ch", code2, "b");

  auto senders = pm.PairedSenders("ch");
  EXPECT_EQ(senders.size(), 2);
}

// --- SessionResolver ---

TEST(SessionResolverTest, MainScope) {
  auto key = ravbot::SessionResolver::ResolveSessionKey(
      ravbot::DmScope::kMain, "main", "discord", "user1");
  EXPECT_EQ(key, "agent:main:main");
}

TEST(SessionResolverTest, PerPeerScope) {
  auto key = ravbot::SessionResolver::ResolveSessionKey(
      ravbot::DmScope::kPerPeer, "main", "discord", "user1");
  EXPECT_EQ(key, "agent:main:peer:user1");
}

TEST(SessionResolverTest, PerChannelPeerScope) {
  auto key = ravbot::SessionResolver::ResolveSessionKey(
      ravbot::DmScope::kPerChannelPeer, "main", "discord", "user1");
  EXPECT_EQ(key, "agent:main:discord:user1");
}

TEST(SessionResolverTest, PerAccountChannelPeerScope) {
  auto key = ravbot::SessionResolver::ResolveSessionKey(
      ravbot::DmScope::kPerAccountChannelPeer, "main", "discord",
      "user1", "acct1");
  EXPECT_EQ(key, "agent:main:acct1:discord:user1");
}

TEST(SessionResolverTest, PerAccountChannelPeerDefaultAccount) {
  auto key = ravbot::SessionResolver::ResolveSessionKey(
      ravbot::DmScope::kPerAccountChannelPeer, "main", "discord",
      "user1");
  EXPECT_EQ(key, "agent:main:default:discord:user1");
}

// --- Group Activation ---

TEST(GroupActivationTest, AlwaysActivates) {
  EXPECT_TRUE(ravbot::SessionResolver::ShouldActivateGroup(
      ravbot::GroupActivation::kAlways, "hello", "Bot"));
}

TEST(GroupActivationTest, MentionDetected) {
  EXPECT_TRUE(ravbot::SessionResolver::ShouldActivateGroup(
      ravbot::GroupActivation::kMention, "Hey @Bot how are you?", "Bot"));
}

TEST(GroupActivationTest, MentionCaseInsensitive) {
  EXPECT_TRUE(ravbot::SessionResolver::ShouldActivateGroup(
      ravbot::GroupActivation::kMention, "Hello @bot!", "Bot"));
}

TEST(GroupActivationTest, NoMentionNoActivation) {
  EXPECT_FALSE(ravbot::SessionResolver::ShouldActivateGroup(
      ravbot::GroupActivation::kMention, "Hello everyone!", "Bot"));
}

TEST(GroupActivationTest, CustomMentionPattern) {
  std::vector<std::string> patterns = {"<@\\d+>"};
  EXPECT_TRUE(ravbot::SessionResolver::ShouldActivateGroup(
      ravbot::GroupActivation::kMention, "Hey <@12345> help",
      "", patterns));
}

TEST(GroupActivationTest, NoMatchWithEmptyBotName) {
  EXPECT_FALSE(ravbot::SessionResolver::ShouldActivateGroup(
      ravbot::GroupActivation::kMention, "Hello world", ""));
}
