// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/qsfp_service/platforms/wedge/WedgeManager.h"

#include "fboss/agent/FbossError.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/lib/fpga/MultiPimPlatformSystemContainer.h"
#include "fboss/qsfp_service/QsfpConfig.h"
#include "fboss/qsfp_service/if/gen-cpp2/qsfp_service_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#include "fboss/qsfp_service/module/ModuleStateMachine.h"
#include "fboss/qsfp_service/module/QsfpModule.h"
#include "fboss/qsfp_service/module/cmis/CmisModule.h"
#include "fboss/qsfp_service/module/sff/Sff8472Module.h"
#include "fboss/qsfp_service/module/sff/SffModule.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeQsfp.h"
#include "folly/futures/Future.h"

#include <fb303/ThreadCachedServiceData.h>

#include <folly/FileUtil.h>
#include <folly/gen/Base.h>
#include <folly/json.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <chrono>

DEFINE_bool(
    override_program_iphy_ports_for_test,
    false,
    "Override wedge_agent programInternalPhyPorts(). For test only");

namespace facebook {
namespace fboss {

namespace {

constexpr int kSecAfterModuleOutOfReset = 2;

static const std::unordered_set<TransceiverID> kEmptryTransceiverIDs = {};

} // namespace

using LockedTransceiversPtr = folly::Synchronized<
    std::map<TransceiverID, std::unique_ptr<Transceiver>>>::WLockedPtr;

WedgeManager::WedgeManager(
    std::unique_ptr<TransceiverPlatformApi> api,
    std::unique_ptr<PlatformMapping> platformMapping,
    PlatformMode mode)
    : TransceiverManager(std::move(api), std::move(platformMapping)),
      platformMode_(mode) {
  /* Constructor for WedgeManager class:
   * Get the TransceiverPlatformApi object from the creator of this object,
   * this object will be used for controlling the QSFP devices on board.
   * Going foward the qsfpPlatApi_ will be used to controll the QSFP devices
   * on FPGA managed platforms and the wedgeI2cBus_ will be used to control
   * the QSFP devices on I2C/CPLD managed platforms
   */
}

void WedgeManager::loadConfig() {
  agentConfig_ = AgentConfig::fromDefaultFile();

  // Process agent config info here.
  for (const auto& port : *agentConfig_->thrift.sw()->ports()) {
    // Get the transceiver id based on the port info from config.
    auto portId = *port.logicalID();
    auto transceiverId = getTransceiverID(PortID(portId));
    if (!transceiverId) {
      XLOG(ERR) << "Did not find transceiver id for port id " << portId;
      continue;
    }
    // Add the port to the transceiver indexed port group.
    auto portGroupIt = portGroupMap_.find(transceiverId.value());
    if (portGroupIt == portGroupMap_.end()) {
      portGroupMap_[transceiverId.value()] = std::set<cfg::Port>{port};
    } else {
      portGroupIt->second.insert(port);
    }
    std::string portName = "";
    if (auto name = port.name()) {
      portName = *name;
      portNameToModule_[portName] = transceiverId.value();
    }
    XLOG(INFO) << "Added port " << portName << " with portId " << portId
               << " to transceiver " << transceiverId.value();
  }

  // Process QSFP config here
  qsfpConfig_ = QsfpConfig::fromDefaultFile();
}

void WedgeManager::initTransceiverMap() {
  // If we can't get access to the USB devices, don't bother to
  // create the QSFP objects;  this is likely to be a permanent
  // error.
  try {
    wedgeI2cBus_ = getI2CBus();
  } catch (const I2cError& ex) {
    XLOG(ERR) << "failed to initialize I2C interface: " << ex.what();
    return;
  }

  // Initialize port status map for transceivers.
  for (int idx = 0; idx < getNumQsfpModules(); idx++) {
    ports_.wlock()->emplace(
        TransceiverID(idx), std::map<uint32_t, PortStatus>());
  }

  // Check if a cold boot has been forced
  if (!canWarmBoot()) {
    XLOG(INFO) << "[COLD Boot] Will trigger all " << getNumQsfpModules()
               << " qsfps hard reset";
    for (int idx = 0; idx < getNumQsfpModules(); idx++) {
      try {
        // Force hard resets on the transceivers which forces a cold boot of the
        // modules.
        triggerQsfpHardReset(idx);
      } catch (const std::exception& ex) {
        XLOG(ERR) << "failed to triggerQsfpHardReset at idx " << idx << ": "
                  << ex.what();
      }
    }
  }

  // Also try to load the config file here so that we have transceiver to port
  // mapping and port name recognization.
  loadConfig();

  // Set overrideTcvrToPortAndProfileForTest_ if
  // FLAGS_override_program_iphy_ports_for_test true.
  setOverrideTcvrToPortAndProfileForTesting();

  refreshTransceivers();
}

void WedgeManager::getTransceiversInfo(
    std::map<int32_t, TransceiverInfo>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "Received request for getTransceiversInfo, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is invalid, just skip and continue to the next.
      continue;
    }
    try {
      auto tcvrID = TransceiverID(i);
      info.insert({i, getTransceiverInfo(tcvrID)});
      if (FLAGS_use_new_state_machine) {
        info[i].stateMachineState() = getCurrentState(tcvrID);
      }
    } catch (const std::exception& ex) {
      XLOG(ERR) << "Transceiver " << i
                << ": Error calling getTransceiverInfo(): " << ex.what();
    }
  }
}

void WedgeManager::getTransceiversRawDOMData(
    std::map<int32_t, RawDOMData>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "Received request for getTransceiversRawDOMData, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }
  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    RawDOMData data;
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      try {
        data = it->second->getRawDOMData();
      } catch (const std::exception& ex) {
        XLOG(ERR) << "Transceiver " << i
                  << ": Error calling getRawDOMData(): " << ex.what();
      }
      info[i] = data;
    }
  }
}

void WedgeManager::getTransceiversDOMDataUnion(
    std::map<int32_t, DOMDataUnion>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "Received request for getTransceiversDOMDataUnion, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }
  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    DOMDataUnion data;
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      try {
        data = it->second->getDOMDataUnion();
      } catch (const std::exception& ex) {
        XLOG(ERR) << "Transceiver " << i
                  << ": Error calling getDOMDataUnion(): " << ex.what();
      }
      info[i] = data;
    }
  }
}

void WedgeManager::readTransceiverRegister(
    std::map<int32_t, ReadResponse>& responses,
    std::unique_ptr<ReadRequest> request) {
  auto ids = *(request->ids());
  XLOG(INFO) << "Received request for reading transceiver registers for ids: "
             << (ids.size() > 0 ? folly::join(",", ids) : "None");
  auto lockedTransceivers = transceivers_.rlock();

  std::vector<folly::Future<std::pair<int32_t, std::unique_ptr<IOBuf>>>>
      futResponses;
  for (const auto& i : ids) {
    // Initialize responses with valid = false. This will be overwritten with
    // the correct valid flag later
    responses[i].valid() = false;
    if (isValidTransceiver(i)) {
      if (auto it = lockedTransceivers->find(TransceiverID(i));
          it != lockedTransceivers->end()) {
        auto param = *(request->parameter());
        futResponses.push_back(it->second->futureReadTransceiver(param));
      }
    }
  }

  folly::collectAllUnsafe(futResponses)
      .thenValue([&responses](
                     const std::vector<folly::Try<
                         std::pair<int32_t, std::unique_ptr<IOBuf>>>>& tries) {
        for (const auto& tryResponse : tries) {
          ReadResponse resp;
          auto tcvrId = tryResponse.value().first;
          resp.data() = *(tryResponse.value().second);
          resp.valid() = resp.data()->length() > 0;
          responses[tcvrId] = resp;
        }
      })
      .wait();
}

void WedgeManager::writeTransceiverRegister(
    std::map<int32_t, WriteResponse>& responses,
    std::unique_ptr<WriteRequest> request) {
  auto ids = *(request->ids());
  XLOG(INFO) << "Received request for writing transceiver register for ids: "
             << (ids.size() > 0 ? folly::join(",", ids) : "None");
  auto lockedTransceivers = transceivers_.rlock();

  std::vector<folly::Future<std::pair<int32_t, bool>>> futResponses;

  for (const auto& i : ids) {
    // Initialize responses with success = false. This will be overwritten with
    // the correct success flag later
    responses[i].success() = false;

    if (isValidTransceiver(i)) {
      if (auto it = lockedTransceivers->find(TransceiverID(i));
          it != lockedTransceivers->end()) {
        auto param = *(request->parameter());
        futResponses.push_back(
            it->second->futureWriteTransceiver(param, *(request->data())));
      }
    }
  }

  folly::collectAllUnsafe(futResponses)
      .thenValue(
          [&responses](
              const std::vector<folly::Try<std::pair<int32_t, bool>>>& tries) {
            for (const auto& tryResponse : tries) {
              WriteResponse resp;
              auto tcvrId = tryResponse.value().first;
              resp.success() = tryResponse.value().second;
              responses[tcvrId] = resp;
            }
          })
      .wait();
}

void WedgeManager::customizeTransceiver(int32_t idx, cfg::PortSpeed speed) {
  if (!isValidTransceiver(idx)) {
    return;
  }
  auto lockedTransceivers = transceivers_.rlock();
  if (auto it = lockedTransceivers->find(TransceiverID(idx));
      it != lockedTransceivers->end()) {
    try {
      it->second->customizeTransceiver(speed);
    } catch (const std::exception& ex) {
      XLOG(ERR) << "Transceiver " << idx
                << ": Error calling customizeTransceiver(): " << ex.what();
    }
  }
}

void WedgeManager::syncPorts(
    std::map<int32_t, TransceiverInfo>& info,
    std::unique_ptr<std::map<int32_t, PortStatus>> ports) {
  // With the new state machine, we don't need to rely on this function to
  // update the ports_(Port status map). But because we're still on the process
  // of moving the trigger of publishing link snapshots from services to the
  // nmt. We need to make sure whether there's a link change, qsfp_service
  // will be still able to publish these snapshots.
  // TODO(joseph5wu) Eventually we don't need to have wedge_agent to syncPorts
  // with qsfp_service when we fully switch to use new state machine and also
  // removing the publishing snapshots logic from qsfp_services
  if (FLAGS_use_new_state_machine) {
    std::set<TransceiverID> tcvrIDs;
    for (const auto& [portID, portStatus] : *ports) {
      if (auto tcvrIdx = portStatus.transceiverIdx()) {
        tcvrIDs.insert(TransceiverID(*tcvrIdx->transceiverId()));
      }
    }
    // Update Transceiver active state
    updateTransceiverActiveState(tcvrIDs, *ports);
    // Only fetch the transceivers for the input ports
    for (auto tcvrID : tcvrIDs) {
      auto lockedTransceivers = transceivers_.rlock();
      if (auto it = lockedTransceivers->find(tcvrID);
          it != lockedTransceivers->end()) {
        try {
          info[tcvrID] = it->second->getTransceiverInfo();
        } catch (const std::exception& ex) {
          XLOG(ERR) << "Transceiver " << tcvrID
                    << ": Error calling getTransceiverInfo(): " << ex.what();
        }
      }
    }
  } else {
    auto groups = folly::gen::from(*ports) |
        folly::gen::filter([](const std::pair<int32_t, PortStatus>& item) {
                    return item.second.transceiverIdx();
                  }) |
        folly::gen::groupBy([](const std::pair<int32_t, PortStatus>& item) {
                    return *item.second.transceiverIdx()
                                .value_unchecked()
                                .transceiverId();
                  }) |
        folly::gen::as<std::vector>();

    auto lockedTransceivers = transceivers_.rlock();
    auto lockedPorts = ports_.wlock();
    for (auto& group : groups) {
      int32_t transceiverIdx = group.key();
      auto tcvrID = TransceiverID(transceiverIdx);
      XLOG(INFO) << "Syncing ports of transceiver " << transceiverIdx;
      if (!isValidTransceiver(transceiverIdx)) {
        continue;
      }

      // Update the PortStatus map in WedgeManager.
      for (auto portStatus : group.values()) {
        lockedPorts->at(tcvrID)[portStatus.first] = portStatus.second;
      }

      if (auto it = lockedTransceivers->find(tcvrID);
          it != lockedTransceivers->end()) {
        try {
          auto transceiver = it->second.get();
          transceiver->transceiverPortsChanged(lockedPorts->at(tcvrID));
          info[transceiverIdx] = transceiver->getTransceiverInfo();
        } catch (const std::exception& ex) {
          XLOG(ERR) << "Transceiver " << transceiverIdx
                    << ": Error calling syncPorts(): " << ex.what();
        }
      } else {
        XLOG(ERR) << "Syncing ports to a transceiver that is not present.";
      }
    }
  }
}

// NOTE: this may refresh transceivers multiple times if they're newly plugged
//  in, as refresh() is called both via updateTransceiverMap and futureRefresh
std::vector<TransceiverID> WedgeManager::refreshTransceivers() {
  try {
    wedgeI2cBus_->verifyBus(false);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Error calling verifyBus(): " << ex.what();
    return {};
  }

  clearAllTransceiverReset();

  // Since transceivers may appear or disappear, we need to update our
  // transceiver mapping and type here.
  updateTransceiverMap();

  // Finally refresh all transceivers without specifying any ids
  return TransceiverManager::refreshTransceivers(kEmptryTransceiverIDs);
}

int WedgeManager::scanTransceiverPresence(
    std::unique_ptr<std::vector<int32_t>> ids) {
  // If the id list is empty, we default to scan the presence of all the
  // transcievers.
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  std::map<int32_t, ModulePresence> presenceUpdate;
  for (auto id : *ids) {
    presenceUpdate[id] = ModulePresence::UNKNOWN;
  }

  wedgeI2cBus_->scanPresence(presenceUpdate);

  int numTransceiversUp = 0;
  for (const auto& presence : presenceUpdate) {
    if (presence.second == ModulePresence::PRESENT) {
      numTransceiversUp++;
    }
  }
  return numTransceiversUp;
}

void WedgeManager::clearAllTransceiverReset() {
  qsfpPlatApi_->clearAllTransceiverReset();
  // Required delay time between a transceiver getting out of reset and fully
  // functional.
  sleep(kSecAfterModuleOutOfReset);
}

void WedgeManager::triggerQsfpHardReset(int idx) {
  auto lockedTransceivers = transceivers_.wlock();
  triggerQsfpHardResetLocked(idx, lockedTransceivers);
}

void WedgeManager::triggerQsfpHardResetLocked(
    int idx,
    LockedTransceiversPtr& lockedTransceivers) {
  // This api accepts 1 based module id however the module id in
  // WedgeManager is 0 based.
  qsfpPlatApi_->triggerQsfpHardReset(idx + 1);

  if (auto it = lockedTransceivers->find(TransceiverID(idx));
      it != lockedTransceivers->end()) {
    lockedTransceivers->erase(it);
  }
}

std::unique_ptr<TransceiverI2CApi> WedgeManager::getI2CBus() {
  return std::make_unique<WedgeI2CBusLock>(std::make_unique<WedgeI2CBus>());
}

void WedgeManager::updateTransceiverMap() {
  std::vector<folly::Future<TransceiverManagementInterface>> futInterfaces;
  std::vector<std::unique_ptr<WedgeQsfp>> qsfpImpls;
  for (int idx = 0; idx < getNumQsfpModules(); idx++) {
    qsfpImpls.push_back(std::make_unique<WedgeQsfp>(idx, wedgeI2cBus_.get()));
    futInterfaces.push_back(
        qsfpImpls[idx]->futureGetTransceiverManagementInterface());
  }
  folly::collectAllUnsafe(futInterfaces.begin(), futInterfaces.end()).wait();
  // After we have collected all transceivers, get the write lock on
  // transceivers_ before updating it
  auto lockedTransceivers = transceivers_.wlock();
  auto lockedPorts = ports_.rlock();
  auto numModules = getNumQsfpModules();
  CHECK_EQ(qsfpImpls.size(), numModules);
  for (int idx = 0; idx < numModules; idx++) {
    if (!futInterfaces[idx].isReady()) {
      XLOG(ERR) << "failed getting TransceiverManagementInterface at " << idx;
      continue;
    }
    auto it = lockedTransceivers->find(TransceiverID(idx));
    if (it != lockedTransceivers->end()) {
      // In the case where we already have a transceiver recorded, try to check
      // whether they match the transceiver type.
      if (it->second->managementInterface() == futInterfaces[idx].value()) {
        // The management interface matches. Nothing needs to be done.
        continue;
      } else {
        // The management changes. Need to Delete the old module to make place
        // for the new one.
        lockedTransceivers->erase(it);
      }
    }

    // Either we don't have a transceiver here before or we had a new one since
    // the management interface changed, we want to create a new module here.
    int portsPerTransceiver =
        (portGroupMap_.size() == 0 ? numPortsPerTransceiver()
                                   : portGroupMap_[idx].size());
    if (futInterfaces[idx].value() == TransceiverManagementInterface::CMIS) {
      XLOG(INFO) << "making CMIS QSFP for " << idx;
      lockedTransceivers->emplace(
          TransceiverID(idx),
          std::make_unique<CmisModule>(
              this, std::move(qsfpImpls[idx]), portsPerTransceiver));
    } else if (
        futInterfaces[idx].value() == TransceiverManagementInterface::SFF) {
      XLOG(INFO) << "making Sff QSFP for " << idx;
      lockedTransceivers->emplace(
          TransceiverID(idx),
          std::make_unique<SffModule>(
              this, std::move(qsfpImpls[idx]), portsPerTransceiver));
    } else if (
        futInterfaces[idx].value() == TransceiverManagementInterface::SFF8472) {
      XLOG(INFO) << "making Sff8472 module for " << idx;
      lockedTransceivers->emplace(
          TransceiverID(idx),
          std::make_unique<Sff8472Module>(this, std::move(qsfpImpls[idx]), 1));
    } else {
      XLOG(ERR) << "Unknown Transceiver interface: "
                << static_cast<int>(futInterfaces[idx].value()) << " at idx "
                << idx;

      try {
        if (!qsfpImpls[idx]->detectTransceiver()) {
          XLOG(DBG3) << "Transceiver is not present at idx " << idx;
          continue;
        }
      } catch (const std::exception& ex) {
        XLOG(ERR) << "failed to detect transceiver at idx " << idx << ": "
                  << ex.what();
        continue;
      }
      // There are times when a module cannot be read however it's present.
      // Try to reset here since that may be able to bring it back.
      bool safeToReset = false;
      TransceiverID id = TransceiverID(idx);
      // Check if we have expected ports info synced over and if all of
      // the ports are down. If any of them is not down then we will not
      // perform the reset.
      if (FLAGS_use_new_state_machine) {
        safeToReset = areAllPortsDown(id);
      } else {
        // TODO(joseph5wu) Deprecate legacy WedgeManager::ports_
        if (auto iter = lockedPorts->find(id); iter != lockedPorts->end()) {
          safeToReset = (iter->second.size() == portsPerTransceiver) &&
              std::all_of(iter->second.begin(),
                          iter->second.end(),
                          [](const auto& port) {
                            return !(*port.second.up());
                          });
        }
      }
      if (safeToReset && (std::time(nullptr) > pauseRemediationUntil_)) {
        XLOG(INFO) << "A present transceiver with unknown interface at " << idx
                   << " Try reset.";
        try {
          triggerQsfpHardResetLocked(idx, lockedTransceivers);
        } catch (const std::exception& ex) {
          XLOG(ERR) << "failed to triggerQsfpHardReset at idx " << idx << ": "
                    << ex.what();
          continue;
        }
      } else {
        XLOG(ERR) << "Unknown interface of transceiver with ports up at "
                  << idx;
      }
      continue;
    }

    // TODO(joseph5wu) Deprecate legacy WedgeManager::ports_
    // New state machine will use refreshStateMachine() to trigger different
    // events so we don't need to rely on another transceiverPortsChanged()
    // call here like legacy state machine
    if (!FLAGS_use_new_state_machine) {
      // Feed its port status to the newly constructed transceiver.
      // However skip if ports have not been synced initially.
      // transceiverPortsChanged will call refreshLocked which takes close to a
      // second for a transceiver. Calling it for every transceiver at
      // initialization is time consuming. Leaving that for refreshTransceivers
      // which runs concurrently for each transceiver.
      if (auto iter = lockedPorts->find(TransceiverID(idx));
          iter != lockedPorts->end() && !iter->second.empty()) {
        try {
          lockedTransceivers->at(TransceiverID(idx))
              ->transceiverPortsChanged(iter->second);
        } catch (const std::exception& ex) {
          XLOG(ERR) << "Transceiver " << idx
                    << ": Error calling transceiverPortsChanged: " << ex.what();
        }
      }
    }
  }
}

/* Get the i2c transaction counters from TranscieverManager base class
 * and update to fbagent. The TransceieverManager base class is inherited
 * by platform speficic Transaceiver Manager class like WedgeManager.
 * That class has the function to get the I2c transaction status.
 */
void WedgeManager::publishI2cTransactionStats() {
  // Get the i2c transaction stats from TransactionManager class (its
  // sub-class having platform specific implementation)
  auto counters = getI2cControllerStats();

  if (counters.size() == 0)
    return;

  // Populate the i2c stats per pim and per controller

  for (const I2cControllerStats& counter : counters) {
    // Publish all the counters to FbAgent

    auto statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".readTotal");
    tcData().setCounter(statName, *counter.readTotal_());

    statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".readFailed");
    tcData().setCounter(statName, *counter.readFailed_());

    statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".readBytes");
    tcData().setCounter(statName, *counter.readBytes_());

    statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".writeTotal");
    tcData().setCounter(statName, *counter.writeTotal_());

    statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".writeFailed");
    tcData().setCounter(statName, *counter.writeFailed_());

    statName = folly::to<std::string>(
        "qsfp.", *counter.controllerName_(), ".writeBytes");
    tcData().setCounter(statName, *counter.writeBytes_());
  }
}

/*
 * This is introduced mainly due to the mismatch of ODS reporting frequency
 * and the interval of us reading transceiver data. Some of the clear on read
 * information may be lost in this process and not being captured in the ODS
 * time series. This would bring difficulty in root cause link issues. Thus
 * here we provide a way of read and clear the data for the purpose of ODS
 * data reporting.
 */
void WedgeManager::getAndClearTransceiversSignalFlags(
    std::map<int32_t, SignalFlags>& signalFlagsMap,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "getAndClearTransceiversSignalFlags, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    SignalFlags signalFlags;
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      signalFlags = it->second->readAndClearCachedSignalFlags();
      signalFlagsMap[i] = signalFlags;
    }
  }
}

void WedgeManager::getAndClearTransceiversMediaSignals(
    std::map<int32_t, std::map<int, MediaLaneSignals>>& mediaSignalsMap,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "getAndClearTransceiversMediaSignals, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    std::map<int, MediaLaneSignals> mediaSignals;
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      mediaSignals = it->second->readAndClearCachedMediaLaneSignals();
      mediaSignalsMap[i] = mediaSignals;
    }
  }
}

/*
 * triggerVdmStatsCapture
 *
 * This function triggers the next VDM data capture for the list of transceiver
 * Id's to be displayed in ODS
 */
void WedgeManager::triggerVdmStatsCapture(std::vector<int32_t>& ids) {
  XLOG(DBG2) << "triggerVdmStatsCapture, with ids: "
             << (ids.size() > 0 ? folly::join(",", ids) : "None");
  if (ids.empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(ids);
  }

  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      // Calling the trigger VDM stats capure function for transceiver
      try {
        it->second->triggerVdmStatsCapture();
      } catch (std::exception& e) {
        XLOG(ERR) << "Transceiver VDM could not be reset for port "
                  << TransceiverID(i) << " message: " << e.what();
        continue;
      }
    }
  }
}

void WedgeManager::getAndClearTransceiversModuleStatus(
    std::map<int32_t, ModuleStatus>& moduleStatusMap,
    std::unique_ptr<std::vector<int32_t>> ids) {
  XLOG(INFO) << "getAndClearTransceiversModuleStatus, with ids: "
             << (ids->size() > 0 ? folly::join(",", *ids) : "None");
  if (ids->empty()) {
    folly::gen::range(0, getNumQsfpModules()) | folly::gen::appendTo(*ids);
  }

  auto lockedTransceivers = transceivers_.rlock();
  for (const auto& i : *ids) {
    if (!isValidTransceiver(i)) {
      // If the transceiver idx is not valid,
      // just skip and continue to the next.
      continue;
    }
    if (auto it = lockedTransceivers->find(TransceiverID(i));
        it != lockedTransceivers->end()) {
      moduleStatusMap[i] = it->second->readAndClearCachedModuleStatus();
    }
  }
}

/*
 * getPhyPortConfigValues
 *
 * This function takes the portId and port profile id. Based on these it looks
 * into the platform mapping for the given platform and extracts information
 * to fill in the phy port config. The output of this function is phy port
 * config structure which can be used later to send to External Phy functions
 */
std::optional<phy::PhyPortConfig> WedgeManager::getPhyPortConfigValues(
    int32_t portId,
    cfg::PortProfileID portProfileId) {
  phy::PhyPortConfig phyPortConfig;

  // First verify if the platform mapping exist for this platform
  if (platformMapping_.get() == nullptr) {
    XLOG(INFO) << "Platform mapping is not present for this platform, exiting";
    return std::nullopt;
  }

  // String value of profile id for printing in log
  std::string portProfileIdStr =
      apache::thrift::util::enumNameSafe(portProfileId);

  // Get port profile config for the given port profile id
  auto portProfileConfig = platformMapping_->getPortProfileConfig(
      PlatformPortProfileConfigMatcher(portProfileId, PortID(portId)));
  if (!portProfileConfig.has_value()) {
    XLOG(INFO) << "For port profile id " << portProfileIdStr
               << ", the supported profile not found in platform mapping";
    return std::nullopt;
  }

  // Get the platform port entry for the given port id
  auto platformPortEntry = platformMapping_->getPlatformPorts().find(portId);
  if (platformPortEntry == platformMapping_->getPlatformPorts().end()) {
    XLOG(INFO) << "For port " << portId
               << ", the platform port not found in platform mapping";
    return std::nullopt;
  }

  // From the above platform port entry, get the port config for the given port
  // profile id
  auto platformPortConfig =
      platformPortEntry->second.supportedProfiles()->find(portProfileId);
  if (platformPortConfig ==
      platformPortEntry->second.supportedProfiles()->end()) {
    XLOG(INFO) << "For port id " << portId << " port profile id "
               << portProfileIdStr
               << ", the supported profile not found in platform mapping";
    return std::nullopt;
  }

  // Get the line polarity swap map
  auto linePolaritySwapMap = utility::getXphyLinePolaritySwapMap(
      platformPortEntry->second,
      portProfileId,
      platformMapping_->getChips(),
      *portProfileConfig);

  // Build the PhyPortConfig using platform port config pins list, polrity swap
  // map, port profile config
  phyPortConfig.config = phy::ExternalPhyConfig::fromConfigeratorTypes(
      *platformPortConfig->second.pins(), linePolaritySwapMap);

  phyPortConfig.profile =
      phy::ExternalPhyProfileConfig::fromPortProfileConfig(*portProfileConfig);

  // Return true
  return phyPortConfig;
}

phy::PhyInfo WedgeManager::getXphyInfo(PortID portID) {
  if (phyManager_ == nullptr) {
    throw FbossError("Unable to get xphy info when PhyManager is not set");
  }

  if (auto phyInfoOpt = phyManager_->getXphyInfo(portID)) {
    return *phyInfoOpt;
  } else {
    throw FbossError("Unable to get xphy info for port: ", portID);
  }
}

void WedgeManager::programXphyPort(
    PortID portId,
    cfg::PortProfileID portProfileId) {
  if (phyManager_ == nullptr) {
    throw FbossError("Unable to program xphy port when PhyManager is not set");
  }

  std::optional<TransceiverInfo> itTcvr;
  // Get the transceiver id for the given port id
  if (auto tcvrID = getTransceiverID(portId)) {
    auto lockedTransceivers = transceivers_.rlock();
    if (auto it = lockedTransceivers->find(*tcvrID);
        it != lockedTransceivers->end()) {
      itTcvr = it->second->getTransceiverInfo();
    } else {
      XLOG(WARNING) << "Port:" << portId
                    << " doesn't have transceiver info for transceiver id:"
                    << *tcvrID;
    }
  }

  phyManager_->programOnePort(portId, portProfileId, itTcvr);
}

bool WedgeManager::initExternalPhyMap() {
  if (!phyManager_) {
    // If there's no PhyManager for such platform, skip init xphy map
    return true;
  }

  // First call PhyManager::initExternalPhyMap() to create xphy map
  auto rb = phyManager_->initExternalPhyMap();
  bool warmboot = canWarmBoot();

  // And then initialize the xphy for each pim which has xphy
  std::vector<folly::Future<folly::Unit>> initPimTasks;
  std::chrono::steady_clock::time_point begin =
      std::chrono::steady_clock::now();
  for (int pimIndex = 0; pimIndex < phyManager_->getNumOfSlot(); ++pimIndex) {
    auto pimID =
        PimID(pimIndex + phyManager_->getSystemContainer()->getPimStartNum());
    if (!phyManager_->shouldInitializePimXphy(pimID)) {
      XLOG(WARN) << "Skip intializing pim xphy for pim="
                 << static_cast<int>(pimID);
      continue;
    }
    XLOG(DBG1) << "[" << (warmboot ? "WARM" : "COLD")
               << " Boot] Initializing PIM " << static_cast<int>(pimID);
    auto* pimEventBase = phyManager_->getPimEventBase(pimID);
    initPimTasks.push_back(
        folly::via(pimEventBase)
            .thenValue([&, pimID, warmboot](auto&&) {
              phyManager_->initializeSlotPhys(pimID, warmboot);
            })
            .thenError(
                folly::tag_t<std::exception>{},
                [pimID](const std::exception& e) {
                  XLOG(WARNING) << "Exception in initializeSlotPhys() for pim="
                                << static_cast<int>(pimID) << ", "
                                << folly::exceptionStr(e);
                }));
  }

  if (!initPimTasks.empty()) {
    folly::collectAll(initPimTasks).wait();
    XLOG(DBG2) << "Initialized all pims xphy took "
               << std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now() - begin)
                      .count()
               << " seconds";

    // Restore warm boot Phy state after initializing all the phys.
    restoreWarmBootPhyState();
  }
  return rb;
}

void WedgeManager::programXphyPortPrbs(
    PortID portID,
    phy::Side side,
    const phy::PortPrbsState& prbs) {
  phyManager_->setPortPrbs(portID, side, prbs);
}

phy::PortPrbsState WedgeManager::getXphyPortPrbs(
    PortID portID,
    phy::Side side) {
  return phyManager_->getPortPrbs(portID, side);
}

void WedgeManager::updateAllXphyPortsStats() {
  if (!phyManager_) {
    // If there's no PhyManager for such platform, skip updating xphy stats
    return;
  }
  // Then we need to update all the programmed port xphy stats
  phyManager_->updateAllXphyPortsStats();
}

std::vector<PortID> WedgeManager::getMacsecCapablePorts() const {
  if (!phyManager_) {
    return {};
  }
  return phyManager_->getPortsSupportingFeature(
      phy::ExternalPhy::Feature::MACSEC);
}

void WedgeManager::setOverrideTcvrToPortAndProfileForTesting(
    std::optional<OverrideTcvrToPortAndProfile> overrideTcvrToPortAndProfile) {
  if (overrideTcvrToPortAndProfile) {
    overrideTcvrToPortAndProfileForTest_ = *overrideTcvrToPortAndProfile;
  } else if (FLAGS_override_program_iphy_ports_for_test) {
    const auto& chips = platformMapping_->getChips();
    for (auto chip : chips) {
      if (*chip.second.type() != phy::DataPlanePhyChipType::TRANSCEIVER) {
        continue;
      }
      auto tcvrID = TransceiverID(*chip.second.physicalID());
      overrideTcvrToPortAndProfileForTest_[tcvrID] = {};
    }
    // Use Agent config to get the iphy port and profile
    const auto& swConfig = agentConfig_->thrift.sw();
    for (const auto& port : *swConfig->ports()) {
      // Only need ENABLED ports
      if (*port.state() != cfg::PortState::ENABLED) {
        continue;
      }
      // If the SW port has transceiver id, add it to
      // overrideTcvrToPortAndProfile
      if (auto tcvrID = getTransceiverID(PortID(*port.logicalID()))) {
        overrideTcvrToPortAndProfileForTest_[*tcvrID].emplace(
            *port.logicalID(), *port.profileID());
      }
    }
  }
}

std::string WedgeManager::listHwObjects(
    std::vector<HwObjectType>& hwObjects,
    bool cached) const {
  if (!phyManager_) {
    return "";
  }
  return phyManager_->listHwObjects(hwObjects, cached);
}

bool WedgeManager::getSdkState(std::string filename) const {
  if (!phyManager_) {
    return false;
  }
  return phyManager_->getSdkState(filename);
}
} // namespace fboss
} // namespace facebook
