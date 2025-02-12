/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/qsfp_service/test/TransceiverManagerTestHelper.h"

#include "fboss/qsfp_service/module/cmis/CmisModule.h"
#include "fboss/qsfp_service/module/tests/FakeTransceiverImpl.h"
#include "fboss/qsfp_service/module/tests/TransceiverTestsHelper.h"
#include "fboss/qsfp_service/test/hw_test/HwTransceiverUtils.h"

namespace facebook::fboss {

class MockCmisModule : public CmisModule {
 public:
  template <typename XcvrImplT>
  explicit MockCmisModule(
      TransceiverManager* transceiverManager,
      std::unique_ptr<XcvrImplT> qsfpImpl,
      unsigned int portsPerTransceiver)
      : CmisModule(
            transceiverManager,
            std::move(qsfpImpl),
            portsPerTransceiver) {
    ON_CALL(*this, getModuleStateChanged).WillByDefault([this]() {
      // Only return true for the first read so that we can mimic the clear
      // on read register
      return (++moduleStateChangedReadTimes_) == 1;
    });
  }

  MOCK_METHOD0(getModuleStateChanged, bool());

 private:
  uint8_t moduleStateChangedReadTimes_{0};
};

class CmisTest : public TransceiverManagerTestHelper {
 public:
  template <typename XcvrImplT>
  MockCmisModule* overrideCmisModule(
      TransceiverID id,
      int numPortsPerXcvr,
      TransceiverModuleIdentifier identifier =
          TransceiverModuleIdentifier::QSFP_PLUS_CMIS) {
    auto xcvrImpl = std::make_unique<XcvrImplT>(id);
    // This override function use ids starting from 1
    transceiverManager_->overrideMgmtInterface(
        static_cast<int>(id) + 1, uint8_t(identifier));

    auto xcvr = static_cast<MockCmisModule*>(
        transceiverManager_->overrideTransceiverForTesting(
            id,
            std::make_unique<MockCmisModule>(
                transceiverManager_.get(),
                std::move(xcvrImpl),
                numPortsPerXcvr)));

    // Refresh once to make sure the override transceiver finishes refresh
    transceiverManager_->refreshStateMachines();

    return xcvr;
  }
};

// Tests that the transceiverInfo object is correctly populated
TEST_F(CmisTest, cmis200GTransceiverInfoTest) {
  auto xcvrID = TransceiverID(0);
  auto xcvr = overrideCmisModule<Cmis200GTransceiver>(xcvrID, 4);

  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.transceiverManagementInterface());
  EXPECT_EQ(
      info.transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 4);
  EXPECT_EQ(xcvr->numMediaLanes(), 4);
  EXPECT_EQ(info.moduleMediaInterface(), MediaInterfaceCode::FR4_200G);
  EXPECT_EQ(
      xcvr->numMediaLanes(),
      info.settings()->mediaInterface().value_or({}).size());
  for (auto& media : *info.settings()->mediaInterface()) {
    EXPECT_EQ(media.media()->get_smfCode(), SMFMediaInterfaceCode::FR4_200G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::FR4_200G);
  }
  testCachedMediaSignals(xcvr);
  // Check cmisStateChanged
  EXPECT_TRUE(
      info.status() && info.status()->cmisStateChanged() &&
      *info.status()->cmisStateChanged());

  utility::HwTransceiverUtils::verifyDiagsCapability(
      info,
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  // Verify update qsfp data logic
  if (auto status = info.status(); status && status->cmisModuleState()) {
    EXPECT_EQ(*status->cmisModuleState(), CmisModuleState::READY);
    auto cmisData = xcvr->getDOMDataUnion().get_cmis();
    // NOTE the following cmis data are specifically set to 0x11 in
    // FakeTransceiverImpl
    EXPECT_TRUE(cmisData.page14());
    // SNR will be set
    EXPECT_EQ(cmisData.page14()->data()[0], 0x06);
    // Check VDM cache
    EXPECT_FALSE(xcvr->isVdmSupported());
  } else {
    throw FbossError("Missing CMIS Module state");
  }

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");
  tests.verifyFwInfo("2.7", "1.10", "3.101");
  tests.verifyTemp(40.26953125);
  tests.verifyVcc(3.3020);

  std::map<std::string, std::vector<double>> laneDom = {
      {"TxBias", {48.442, 50.082, 53.516, 50.028}},
      {"TxPwr", {2.13, 2.0748, 2.0512, 2.1027}},
      {"RxPwr", {0.4032, 0.3969, 0.5812, 0.5176}},
  };
  tests.verifyLaneDom(laneDom, xcvr->numMediaLanes());

  std::map<std::string, std::vector<bool>> expectedMediaSignals = {
      {"Tx_Los", {1, 1, 0, 1}},
      {"Rx_Los", {1, 0, 1, 0}},
      {"Tx_Lol", {0, 0, 1, 1}},
      {"Rx_Lol", {0, 1, 1, 0}},
      {"Tx_Fault", {0, 1, 0, 1}},
      {"Tx_AdaptFault", {1, 0, 1, 1}},
  };
  tests.verifyMediaLaneSignals(expectedMediaSignals, xcvr->numMediaLanes());

  std::array<bool, 4> expectedDatapathDeinit = {0, 1, 1, 0};
  std::array<CmisLaneState, 4> expectedLaneState = {
      CmisLaneState::ACTIVATED,
      CmisLaneState::DATAPATHINIT,
      CmisLaneState::TX_ON,
      CmisLaneState::DEINIT};

  EXPECT_EQ(xcvr->numHostLanes(), info.hostLaneSignals().value_or({}).size());
  for (auto& signal : *info.hostLaneSignals()) {
    EXPECT_EQ(
        expectedDatapathDeinit[*signal.lane()],
        signal.dataPathDeInit().value_or({}));
    EXPECT_EQ(
        expectedLaneState[*signal.lane()], signal.cmisLaneState().value_or({}));
  }

  std::map<std::string, std::vector<bool>> expectedMediaLaneSettings = {
      {"TxDisable", {0, 1, 0, 1}},
      {"TxSqDisable", {1, 1, 0, 1}},
      {"TxForcedSq", {0, 0, 1, 1}},
  };

  std::map<std::string, std::vector<uint8_t>> expectedHostLaneSettings = {
      {"RxOutDisable", {1, 1, 0, 0}},
      {"RxSqDisable", {0, 0, 1, 1}},
      {"RxEqPrecursor", {2, 2, 2, 2}},
      {"RxEqPostcursor", {0, 0, 0, 0}},
      {"RxEqMain", {3, 3, 3, 3}},
  };

  auto settings = info.settings().value_or({});
  tests.verifyMediaLaneSettings(
      expectedMediaLaneSettings, xcvr->numMediaLanes());
  tests.verifyHostLaneSettings(expectedHostLaneSettings, xcvr->numHostLanes());

  EXPECT_EQ(PowerControlState::HIGH_POWER_OVERRIDE, settings.powerControl());

  std::map<std::string, std::vector<bool>> laneInterrupts = {
      {"TxPwrHighAlarm", {0, 1, 0, 0}},
      {"TxPwrHighWarn", {0, 0, 1, 0}},
      {"TxPwrLowAlarm", {1, 1, 0, 0}},
      {"TxPwrLowWarn", {1, 0, 1, 0}},
      {"RxPwrHighAlarm", {0, 1, 0, 1}},
      {"RxPwrHighWarn", {0, 0, 1, 1}},
      {"RxPwrLowAlarm", {1, 1, 0, 1}},
      {"RxPwrLowWarn", {1, 0, 1, 1}},
      {"TxBiasHighAlarm", {0, 1, 1, 0}},
      {"TxBiasHighWarn", {0, 0, 0, 1}},
      {"TxBiasLowAlarm", {1, 1, 1, 0}},
      {"TxBiasLowWarn", {1, 0, 0, 1}},
  };
  tests.verifyLaneInterrupts(laneInterrupts, xcvr->numMediaLanes());
  tests.verifyGlobalInterrupts("temp", 1, 1, 0, 1);
  tests.verifyGlobalInterrupts("vcc", 1, 0, 1, 0);

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS13Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();
  tests.verifyPrbsPolynomials(expectedPolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedPolynomials, sysPrbsCapability);
}

TEST_F(CmisTest, cmis400GLr4TransceiverInfoTest) {
  auto xcvrID = TransceiverID(1);
  auto xcvr = overrideCmisModule<Cmis400GLr4Transceiver>(xcvrID, 1);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.transceiverManagementInterface());
  EXPECT_EQ(
      info.transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 8);
  EXPECT_EQ(xcvr->numMediaLanes(), 4);
  EXPECT_EQ(info.moduleMediaInterface(), MediaInterfaceCode::LR4_400G_10KM);
  for (auto& media : *info.settings()->mediaInterface()) {
    EXPECT_EQ(media.media()->get_smfCode(), SMFMediaInterfaceCode::LR4_10_400G);
    EXPECT_EQ(media.code(), MediaInterfaceCode::LR4_400G_10KM);
  }
  // Check cmisStateChanged
  EXPECT_TRUE(
      info.status() && info.status()->cmisStateChanged() &&
      *info.status()->cmisStateChanged());

  utility::HwTransceiverUtils::verifyDiagsCapability(
      info,
      transceiverManager_->getDiagsCapability(xcvrID),
      false /* skipCheckingIndividualCapability */);

  // Verify update qsfp data logic
  if (auto status = info.status(); status && status->cmisModuleState()) {
    EXPECT_EQ(*status->cmisModuleState(), CmisModuleState::READY);
    auto cmisData = xcvr->getDOMDataUnion().get_cmis();
    // NOTE the following cmis data are specifically set to 0x11 in
    // FakeTransceiverImpl
    EXPECT_TRUE(cmisData.page14());
    // SNR will be set
    EXPECT_EQ(cmisData.page14()->data()[0], 0x06);
    // Check VDM cache
    EXPECT_TRUE(xcvr->isVdmSupported());
    EXPECT_TRUE(cmisData.page20());
    EXPECT_EQ(cmisData.page20()->data()[0], 0x00);
    EXPECT_TRUE(cmisData.page21());
    EXPECT_EQ(cmisData.page21()->data()[0], 0x00);
    EXPECT_TRUE(cmisData.page24());
    EXPECT_EQ(cmisData.page24()->data()[0], 0x15);
    EXPECT_TRUE(cmisData.page25());
    EXPECT_EQ(cmisData.page25()->data()[0], 0x00);
  } else {
    throw FbossError("Missing CMIS Module state");
  }

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");

  auto diagsCap = transceiverManager_->getDiagsCapability(xcvrID);
  EXPECT_TRUE(diagsCap.has_value());
  std::vector<prbs::PrbsPolynomial> expectedSysPolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS15Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q};
  std::vector<prbs::PrbsPolynomial> expectedLinePolynomials = {
      prbs::PrbsPolynomial::PRBS31Q,
      prbs::PrbsPolynomial::PRBS23Q,
      prbs::PrbsPolynomial::PRBS9Q,
      prbs::PrbsPolynomial::PRBS7Q};

  auto linePrbsCapability = *(*diagsCap).prbsLineCapabilities();
  auto sysPrbsCapability = *(*diagsCap).prbsSystemCapabilities();

  tests.verifyPrbsPolynomials(expectedLinePolynomials, linePrbsCapability);
  tests.verifyPrbsPolynomials(expectedSysPolynomials, sysPrbsCapability);
}

TEST_F(CmisTest, flatMemTransceiverInfoTest) {
  auto xcvr = overrideCmisModule<CmisFlatMemTransceiver>(TransceiverID(1), 4);
  const auto& info = xcvr->getTransceiverInfo();
  EXPECT_TRUE(info.transceiverManagementInterface());
  EXPECT_EQ(
      info.transceiverManagementInterface(),
      TransceiverManagementInterface::CMIS);
  EXPECT_EQ(xcvr->numHostLanes(), 4);
  EXPECT_EQ(xcvr->numMediaLanes(), 0);

  TransceiverTestsHelper tests(info);
  tests.verifyVendorName("FACETEST");
}

TEST_F(CmisTest, moduleEepromChecksumTest) {
  // Create CMIS 200G FR4 module
  auto xcvrCmis200GFr4 =
      overrideCmisModule<Cmis200GTransceiver>(TransceiverID(1), 4);
  // Verify EEPROM checksum for CMIS 200G FR4 module
  bool csumValid = xcvrCmis200GFr4->verifyEepromChecksums();
  EXPECT_TRUE(csumValid);

  // Create CMIS 400G LR4 module
  auto xcvrCmis400GLr4 =
      overrideCmisModule<Cmis400GLr4Transceiver>(TransceiverID(2), 1);
  // Verify EEPROM checksum for CMIS 400G LR4 module
  csumValid = xcvrCmis400GLr4->verifyEepromChecksums();
  EXPECT_TRUE(csumValid);

  // Create CMIS 200G FR4 Bad module
  auto xcvrCmis200GFr4Bad =
      overrideCmisModule<BadCmis200GTransceiver>(TransceiverID(3), 1);
  // Verify EEPROM checksum Invalid for CMIS 200G FR4 Bad module
  csumValid = xcvrCmis200GFr4Bad->verifyEepromChecksums();
  EXPECT_FALSE(csumValid);
}

// MSM: Not_Present -> Present -> Discovered -> Inactive (on Agent timeout
//      event)
TEST(CmisOldStateMachineTest, testStateToInactive) {
  int idx = 1;
  std::unique_ptr<Cmis200GTransceiver> qsfpImpl =
      std::make_unique<Cmis200GTransceiver>(idx);

  std::unique_ptr<CmisModule> qsfpMod =
      std::make_unique<CmisModule>(nullptr, std::move(qsfpImpl), 4);

  qsfpMod->setLegacyModuleStateMachineModulePointer(qsfpMod.get());

  // MSM: Not_Present -> Present
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::DETECT_TRANSCEIVER);
  int CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 1);

  // MSM: Present -> Discovered
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::READ_EEPROM);
  qsfpMod->refresh();
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 2);

  // MSM: Discovered -> Inactive (on Agent timeout event)
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::AGENT_SYNC_TIMEOUT);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 3);
}

// MSM: Not_Present -> Present -> Discovered -> Inactive (On Agent timeout)
// Note: This test waits for 2 minutes to allow state machine transition to
//       new state
TEST(CmisOldStateMachineTest, testStateTimeoutWaitInactive) {
  int idx = 1;
  std::unique_ptr<Cmis200GTransceiver> qsfpImpl =
      std::make_unique<Cmis200GTransceiver>(idx);

  std::unique_ptr<CmisModule> qsfpMod =
      std::make_unique<CmisModule>(nullptr, std::move(qsfpImpl), 4);

  qsfpMod->setLegacyModuleStateMachineModulePointer(qsfpMod.get());

  // MSM: Not_Present -> Present -> Discovered -> Inactive (On Agent timeout)
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::DETECT_TRANSCEIVER);

  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::READ_EEPROM);
  qsfpMod->refresh();

  // Wait for 2 minutes for Agent sync timeout to happen and module SM
  // to transition to Inactive
  /* sleep override */
  sleep(125);
  int CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 3);
}

// MSM: Not_Present -> Present -> Discovered -> Inactive <-> Active
TEST(CmisOldStateMachineTest, testPsmStates) {
  int idx = 1;
  std::unique_ptr<Cmis200GTransceiver> qsfpImpl =
      std::make_unique<Cmis200GTransceiver>(idx);

  std::unique_ptr<CmisModule> qsfpMod =
      std::make_unique<CmisModule>(nullptr, std::move(qsfpImpl), 4);

  qsfpMod->setLegacyModuleStateMachineModulePointer(qsfpMod.get());

  // MSM: Not_Present -> Present -> Discovered -> Inactive (all port down)
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::DETECT_TRANSCEIVER);
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::READ_EEPROM);
  qsfpMod->refresh();

  qsfpMod->portStateMachines_[0].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  qsfpMod->portStateMachines_[1].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  qsfpMod->portStateMachines_[2].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  qsfpMod->portStateMachines_[3].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  int CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 3);

  // One port Up -> Active
  qsfpMod->portStateMachines_[3].process_event(MODULE_PORT_EVENT_AGENT_PORT_UP);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 4);

  // All ports down -> Inactive state
  qsfpMod->portStateMachines_[3].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 3);

  // All port up -> Active state
  qsfpMod->portStateMachines_[0].process_event(MODULE_PORT_EVENT_AGENT_PORT_UP);
  qsfpMod->portStateMachines_[1].process_event(MODULE_PORT_EVENT_AGENT_PORT_UP);
  qsfpMod->portStateMachines_[2].process_event(MODULE_PORT_EVENT_AGENT_PORT_UP);
  qsfpMod->portStateMachines_[3].process_event(MODULE_PORT_EVENT_AGENT_PORT_UP);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 4);

  // One port down, rest of ports up -> Active state
  qsfpMod->portStateMachines_[0].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 4);
}

// MSM: Not_Present -> Present -> Discovered -> Inactive (Remediation)
TEST(CmisOldStateMachineTest, testPsmRemediation) {
  int idx = 1;
  std::unique_ptr<Cmis200GTransceiver> qsfpImpl =
      std::make_unique<Cmis200GTransceiver>(idx);

  std::unique_ptr<CmisModule> qsfpMod =
      std::make_unique<CmisModule>(nullptr, std::move(qsfpImpl), 2);

  qsfpMod->setLegacyModuleStateMachineModulePointer(qsfpMod.get());

  // MSM: Not_Present -> Present -> Discovered -> Inactive (all port down)
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::DETECT_TRANSCEIVER);
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::READ_EEPROM);
  qsfpMod->refresh();

  EXPECT_EQ(qsfpMod->portStateMachines_.size(), 2);

  qsfpMod->portStateMachines_[0].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  qsfpMod->portStateMachines_[1].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  int CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 3);

  // Bring up done event -> Inactive state
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::BRINGUP_DONE);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 3);

  // Remediation done event -> Inactive state
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::REMEDIATE_DONE);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 3);
}

// MSM: Not_Present -> Present -> Discovered -> Inactive -> Upgrading
TEST(CmisOldStateMachineTest, testMsmFwUpgrading) {
  int idx = 1;
  std::unique_ptr<Cmis200GTransceiver> qsfpImpl =
      std::make_unique<Cmis200GTransceiver>(idx);

  std::unique_ptr<CmisModule> qsfpMod =
      std::make_unique<CmisModule>(nullptr, std::move(qsfpImpl), 2);

  qsfpMod->setLegacyModuleStateMachineModulePointer(qsfpMod.get());

  // MSM: Not_Present -> Present -> Discovered -> Inactive (all port down)
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::DETECT_TRANSCEIVER);
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::READ_EEPROM);
  qsfpMod->refresh();

  qsfpMod->portStateMachines_[0].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  qsfpMod->portStateMachines_[1].process_event(MODULE_PORT_EVENT_AGENT_PORT_UP);
  int CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 4);

  // Upgrading event should get rejected
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::TRIGGER_UPGRADE);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 4);

  // Bring module to Inactive state
  qsfpMod->portStateMachines_[1].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 3);

  // Verify Upgrading event accepted and new state is Upgrading
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::TRIGGER_UPGRADE);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 5);

  // Finish upgrading with module reset and move it to first state
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::RESET_TRANSCEIVER);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 0);
}

// MSM: Not_Present -> Present -> Discovered -> Inactive -> Upgrading (Forced)
TEST(CmisOldStateMachineTest, testMsmFwForceUpgrade) {
  int idx = 1;
  std::unique_ptr<Cmis200GTransceiver> qsfpImpl =
      std::make_unique<Cmis200GTransceiver>(idx);

  std::unique_ptr<CmisModule> qsfpMod =
      std::make_unique<CmisModule>(nullptr, std::move(qsfpImpl), 2);

  qsfpMod->setLegacyModuleStateMachineModulePointer(qsfpMod.get());

  // MSM: Not_Present -> Present -> Discovered -> Inactive (all port down)
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::DETECT_TRANSCEIVER);
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::READ_EEPROM);
  qsfpMod->refresh();

  qsfpMod->portStateMachines_[0].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  qsfpMod->portStateMachines_[1].process_event(MODULE_PORT_EVENT_AGENT_PORT_UP);
  int CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 4);

  // Check Forced upgrade event is accepted and module is in Upgrading state
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::FORCED_UPGRADE);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 5);

  // Finish upgrading with module reset and move it to first state
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::RESET_TRANSCEIVER);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 0);
}

// MSM: Check optics removal event from various states
TEST(CmisOldStateMachineTest, testOpticsRemoval) {
  int idx = 1;
  std::unique_ptr<Cmis200GTransceiver> qsfpImpl =
      std::make_unique<Cmis200GTransceiver>(idx);

  std::unique_ptr<CmisModule> qsfpMod =
      std::make_unique<CmisModule>(nullptr, std::move(qsfpImpl), 2);

  qsfpMod->setLegacyModuleStateMachineModulePointer(qsfpMod.get());

  // Bring up to discovered state and check optics removed event - new state
  // should be first one and PSM should get removed
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::DETECT_TRANSCEIVER);
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::READ_EEPROM);
  qsfpMod->refresh();
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::REMOVE_TRANSCEIVER);
  int CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 0);
  EXPECT_EQ(qsfpMod->portStateMachines_.size(), 0);

  // Bring up to Active state and check optics removed event
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::DETECT_TRANSCEIVER);
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::READ_EEPROM);
  qsfpMod->refresh();
  qsfpMod->portStateMachines_[0].process_event(MODULE_PORT_EVENT_AGENT_PORT_UP);
  qsfpMod->portStateMachines_[1].process_event(MODULE_PORT_EVENT_AGENT_PORT_UP);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 4);
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::REMOVE_TRANSCEIVER);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 0);

  // Bring up to Upgrading state and check optics removed event
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::DETECT_TRANSCEIVER);
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::READ_EEPROM);
  qsfpMod->refresh();
  qsfpMod->portStateMachines_[0].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  qsfpMod->portStateMachines_[1].process_event(
      MODULE_PORT_EVENT_AGENT_PORT_DOWN);
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::TRIGGER_UPGRADE);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 5);
  qsfpMod->legacyModuleStateMachineStateUpdate(
      TransceiverStateMachineEvent::REMOVE_TRANSCEIVER);
  CurrState = qsfpMod->getLegacyModuleStateMachineCurrentState();
  EXPECT_EQ(CurrState, 0);
}
} // namespace facebook::fboss
