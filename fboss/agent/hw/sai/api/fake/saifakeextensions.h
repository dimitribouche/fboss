// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <sai.h>

typedef enum _sai_port_serdes_extensions_attr_t {
  SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_CTLE_CODE =
      SAI_PORT_SERDES_ATTR_CUSTOM_RANGE_START,
  SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_DSP_MODE,
  SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_AFE_TRIM,
  SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_AC_COUPLING_BYPASS,
  SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_AFE_ADAPTIVE_ENABLE,
} sai_port_serdes_extensions_attr_t;

typedef enum _sai_switch_extensions_attr_t {
  SAI_SWITCH_ATTR_EXT_FAKE_LED = SAI_SWITCH_ATTR_END,
  SAI_SWITCH_ATTR_EXT_FAKE_LED_RESET,
  SAI_SWITCH_ATTR_EXT_FAKE_ACL_FIELD_LIST,
  SAI_SWITCH_ATTR_DEFAULT_EGRESS_BUFFER_POOL_SHARED_SIZE,
  SAI_SWITCH_ATTR_EXT_FAKE_HW_ECC_ERROR_INITIATE,
} sai_switch_extensions_attr_t;

typedef enum _sai_tam_event_extensions_attr_t {
  SAI_TAM_EVENT_ATTR_FAKE_SWITCH_EVENT_TYPE = SAI_TAM_EVENT_ATTR_END,
  SAI_TAM_EVENT_ATTR_FAKE_SWITCH_EVENT_ID,
} sai_tam_event_extensions_attr_t;

typedef enum _sai_port_extensions_attr_t {
  SAI_PORT_ATTR_EXT_FAKE_SYSTEM_PORT_ID = SAI_PORT_ATTR_CUSTOM_RANGE_START,
} sai_port_extensions_attr_t;
