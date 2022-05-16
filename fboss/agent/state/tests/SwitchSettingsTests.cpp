/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;
using std::make_shared;

TEST(SwitchSettingsTest, applyL2LearningConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  *config.switchSettings()->l2LearningMode() = cfg::L2LearningMode::SOFTWARE;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);

  auto switchSettingsV1 = stateV1->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(
      cfg::L2LearningMode::SOFTWARE, switchSettingsV1->getL2LearningMode());
  EXPECT_FALSE(switchSettingsV1->isQcmEnable());
  EXPECT_FALSE(switchSettingsV1->isPtpTcEnable());
  EXPECT_EQ(300, switchSettingsV1->getL2AgeTimerSeconds());

  *config.switchSettings()->l2LearningMode() = cfg::L2LearningMode::HARDWARE;

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);

  auto switchSettingsV2 = stateV2->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_FALSE(switchSettingsV2->isPublished());
  EXPECT_EQ(
      cfg::L2LearningMode::HARDWARE, switchSettingsV2->getL2LearningMode());
  EXPECT_FALSE(switchSettingsV2->isQcmEnable());
  EXPECT_FALSE(switchSettingsV2->isPtpTcEnable());
  EXPECT_EQ(300, switchSettingsV1->getL2AgeTimerSeconds());
}

TEST(SwitchSettingsTest, applyQcmConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  *config.switchSettings()->qcmEnable() = true;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = stateV1->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_TRUE(switchSettingsV1->isQcmEnable());
  EXPECT_EQ(
      cfg::L2LearningMode::HARDWARE, switchSettingsV1->getL2LearningMode());
  EXPECT_FALSE(switchSettingsV1->isPtpTcEnable());
  EXPECT_EQ(300, switchSettingsV1->getL2AgeTimerSeconds());

  *config.switchSettings()->qcmEnable() = false;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto switchSettingsV2 = stateV2->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_FALSE(switchSettingsV2->isPublished());
  EXPECT_FALSE(switchSettingsV2->isQcmEnable());
  EXPECT_EQ(
      cfg::L2LearningMode::HARDWARE, switchSettingsV2->getL2LearningMode());
  EXPECT_FALSE(switchSettingsV2->isPtpTcEnable());
}

TEST(SwitchSettingsTest, applyPtpTcEnable) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.switchSettings()->ptpTcEnable() = true;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = stateV1->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_TRUE(switchSettingsV1->isPtpTcEnable());

  config.switchSettings()->ptpTcEnable() = false;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto switchSettingsV2 = stateV2->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV2);
  EXPECT_FALSE(switchSettingsV2->isPublished());
  EXPECT_FALSE(switchSettingsV2->isPtpTcEnable());
  EXPECT_EQ(300, switchSettingsV1->getL2AgeTimerSeconds());
}

TEST(SwitchSettingsTest, applyL2AgeTimerSeconds) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  // Check default value
  auto l2AgeTimerSeconds = 300;
  auto switchSettingsV0 = stateV0->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_FALSE(switchSettingsV0->isPublished());
  EXPECT_EQ(l2AgeTimerSeconds, switchSettingsV0->getL2AgeTimerSeconds());

  // Check if value is updated
  l2AgeTimerSeconds *= 2;
  cfg::SwitchConfig config;
  config.switchSettings()->l2AgeTimerSeconds() = l2AgeTimerSeconds;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = stateV1->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(l2AgeTimerSeconds, switchSettingsV1->getL2AgeTimerSeconds());
}

TEST(SwitchSettingsTest, applyMaxRouteCounterIDs) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  // Check default value
  auto maxRouteCounterIDs = 0;
  auto switchSettingsV0 = stateV0->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_FALSE(switchSettingsV0->isPublished());
  EXPECT_EQ(maxRouteCounterIDs, switchSettingsV0->getMaxRouteCounterIDs());

  // Check if value is updated
  maxRouteCounterIDs = 10;
  cfg::SwitchConfig config;
  config.switchSettings()->maxRouteCounterIDs() = maxRouteCounterIDs;
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = stateV1->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(maxRouteCounterIDs, switchSettingsV1->getMaxRouteCounterIDs());
}

TEST(SwitchSettingsTest, applyBlockNeighbors) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  // Check default value
  auto switchSettingsV0 = stateV0->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_EQ(switchSettingsV0->getBlockNeighbors().size(), 0);

  // Check if value is updated
  cfg::SwitchConfig config;

  cfg::Neighbor blockNeighbor;
  blockNeighbor.vlanID() = 1;
  blockNeighbor.ipAddress() = "1.1.1.1";

  config.switchSettings()->blockNeighbors() = {blockNeighbor};
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = stateV1->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(switchSettingsV1->getBlockNeighbors().size(), 1);

  EXPECT_EQ(
      int(switchSettingsV1->getBlockNeighbors()[0].first),
      blockNeighbor.vlanID());
  EXPECT_EQ(
      switchSettingsV1->getBlockNeighbors()[0].second.str(),
      blockNeighbor.ipAddress());
}

TEST(SwitchSettingsTest, applyMacAddrsToBlock) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  // Check default value
  auto switchSettingsV0 = stateV0->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV0);
  EXPECT_EQ(switchSettingsV0->getMacAddrsToBlock().size(), 0);

  // Check if value is updated
  cfg::SwitchConfig config;

  cfg::MacAndVlan macAddrToBlock;
  macAddrToBlock.vlanID() = 1;
  macAddrToBlock.macAddress() = "00:11:22:33:44:55";

  config.switchSettings()->macAddrsToBlock() = {macAddrToBlock};
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto switchSettingsV1 = stateV1->getSwitchSettings();
  ASSERT_NE(nullptr, switchSettingsV1);
  EXPECT_FALSE(switchSettingsV1->isPublished());
  EXPECT_EQ(switchSettingsV1->getMacAddrsToBlock().size(), 1);

  EXPECT_EQ(
      int(switchSettingsV1->getMacAddrsToBlock()[0].first),
      macAddrToBlock.vlanID());
  EXPECT_EQ(
      switchSettingsV1->getMacAddrsToBlock()[0].second.toString(),
      macAddrToBlock.macAddress());
}

TEST(SwitchSettingsTest, ToFromJSON) {
  std::string jsonStr = R"(
        {
          "l2LearningMode": 1,
          "qcmEnable": true,
          "ptpTcEnable": true,
          "l2AgeTimerSeconds": 600,
          "maxRouteCounterIDs": 10,
          "blockNeighbors": [],
          "macAddrsToBlock": []
        }
  )";

  auto switchSettings =
      SwitchSettings::fromFollyDynamic(folly::parseJson(jsonStr));
  EXPECT_EQ(cfg::L2LearningMode::SOFTWARE, switchSettings->getL2LearningMode());
  EXPECT_TRUE(switchSettings->isQcmEnable());
  EXPECT_TRUE(switchSettings->isPtpTcEnable());
  EXPECT_EQ(600, switchSettings->getL2AgeTimerSeconds());
  EXPECT_EQ(10, switchSettings->getMaxRouteCounterIDs());

  auto dyn1 = switchSettings->toFollyDynamic();
  auto dyn2 = folly::parseJson(jsonStr);

  EXPECT_EQ(dyn1, dyn2);
}
