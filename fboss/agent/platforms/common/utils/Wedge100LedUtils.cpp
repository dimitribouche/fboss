// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/platforms/common/utils/Wedge100LedUtils.h"
#include "fboss/agent/FbossError.h"
#include "fboss/lib/usb/Wedge100I2CBus.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeI2CBusLock.h"

#include <folly/Range.h>
#include <folly/logging/xlog.h>

namespace {
constexpr auto kMaxSetLedTime = std::chrono::seconds(1);
// These constants are from broadcom, see the attachments on T27619604
constexpr uint8_t kSysCpldAddr = 0x32;
constexpr uint8_t kLedModeReg = 0x3c;
constexpr uint8_t kTwelveBitMode = 0x6;
} // namespace

namespace facebook::fboss {

Wedge100LedUtils::LedColor
Wedge100LedUtils::getDesiredLEDState(int numberOfLanes, bool up, bool adminUp) {
  if (!up || !adminUp) {
    return Wedge100LedUtils::LedColor::OFF;
  }

  switch (numberOfLanes) {
    case 4: // Quaid
      return LedColor::BLUE;
    case 2: // DUAL
      return LedColor::MAGENTA;
    case 1: // Single
      return LedColor::GREEN;
  }
  throw FbossError("Unable to determine LED color for port");
}

Wedge100LedUtils::LedColor Wedge100LedUtils::getDesiredLEDState(
    PortLedExternalState externalState,
    Wedge100LedUtils::LedColor currentColor) {
  Wedge100LedUtils::LedColor color = Wedge100LedUtils::LedColor::OFF;
  switch (externalState) {
    case PortLedExternalState::NONE:
      color = currentColor;
      break;
    case PortLedExternalState::CABLING_ERROR:
      color = Wedge100LedUtils::LedColor::YELLOW;
      break;
    case PortLedExternalState::EXTERNAL_FORCE_ON:
      color = Wedge100LedUtils::LedColor::WHITE;
      break;
    case PortLedExternalState::EXTERNAL_FORCE_OFF:
      color = Wedge100LedUtils::LedColor::OFF;
      break;
  }
  return color;
}

int Wedge100LedUtils::getPipe(PortID port) {
  return static_cast<int>(port) / 34;
}

int Wedge100LedUtils::getPortIndex(PortID port) {
  int pipe = getPipe(port);
  int offset = static_cast<int>(port) % 34;
  if (pipe == 0) {
    --offset;
  }
  int index = offset;
  if (pipe == 3 || pipe == 2) {
    index += 32;
  }
  return index;
}

bool Wedge100LedUtils::isTop(std::optional<TransceiverID> tcvrID) {
  if (tcvrID.has_value()) {
    return !((*tcvrID) & 0x1);
  }
  return false;
}

std::pair<int, int>
Wedge100LedUtils::getCompactPortIndexes(PortID port, bool isTop, bool isQuad) {
  if (isTop) {
    return std::make_pair(
        getPortIndex(port), getPortIndex(static_cast<PortID>(port + 4)));
  }
  auto target = isQuad ? port + 2 : port;
  return std::make_pair(
      getPortIndex(static_cast<PortID>(target + 1)),
      getPortIndex(static_cast<PortID>(target - 3)));
}

folly::ByteRange Wedge100LedUtils::defaultLedCode() {
  // clang-format off
  /* Auto-generated using the Broadcom ledasm tool */
static const std::vector<uint8_t> kWedge100LedCode {
    0x02, 0x3F, 0x12, 0xC0, 0xF8, 0x15, 0x67, 0x0D, 0x90, 0x75, 0x02, 0x3A,
    0xC0, 0x21, 0x87, 0x99, 0x21, 0x87, 0x99, 0x21, 0x87, 0x57, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
  // clang-format on
  return folly::ByteRange(kWedge100LedCode.data(), kWedge100LedCode.size());
}

size_t Wedge100LedUtils::getPortOffset(int index) {
  constexpr uint16_t kPortWriteStart = 0xc0;
  return kPortWriteStart + index;
}

std::optional<uint32_t> Wedge100LedUtils::getLEDProcessorNumber(PortID port) {
  int pipe = Wedge100LedUtils::getPipe(static_cast<PortID>(port));
  if (pipe == 0 || pipe == 3) {
    return 0;
  } else if (pipe == 1 || pipe == 2) {
    return 1;
  }
  // We only show link status for the main four pipes.
  return std::nullopt;
}

void Wedge100LedUtils::enableLedMode() {
  // TODO: adding retries adds tolerance in case the i2c bus is
  // busy. Long-term, we should think about having all i2c io go
  // through qsfp_service, though this feels a bit out of place there.
  auto expireTime = std::chrono::steady_clock::now() + kMaxSetLedTime;
  uint8_t mode = kTwelveBitMode;
  while (true) {
    try {
      auto i2cBus = std::make_unique<Wedge100I2CBus>();
      WedgeI2CBusLock(std::move(i2cBus))
          .write(kSysCpldAddr, kLedModeReg, 1, &mode);
      XLOG(INFO) << "Successfully set LED mode to '12-bit' mode";
      return;
    } catch (const std::exception& ex) {
      if (std::chrono::steady_clock::now() > expireTime) {
        XLOG(ERR) << __func__
                  << ": failed to change LED mode: " << folly::exceptionStr(ex);
        return;
      }
    }
    /* sleep override */
    usleep(100);
  }
}
} // namespace facebook::fboss
