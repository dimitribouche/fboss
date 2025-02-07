// Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
// All Rights Reserved.

#include <string>

namespace facebook::fboss::platform {

/* ToDo: replace hardcoded sysfs path with dynamically mapped symbolic path */

std::string getDarwinFSConfig() {
  return R"({
  "bsp" : "darwin",
  "watchdog" : {
    "access" : {
      "source" : "sysfs",
      "path" : "/dev/watchdog0"
    },
    "value" : 1
  },
  "boost_on_dead_fan" : true,
  "boost_on_dead_sensor" : false,
  "boost_on_no_qsfp_after" : 90,
  "pwm_boost_value" : 100,
  "pwm_transition_value" : 50,
  "pwm_percent_lower_limit" : 24,
  "pwm_percent_upper_limit" : 100,
  "shutdown_command" : "wedge_power reset -s",
  "optics" : {
    "qsfp_group_1" : {
      "instance" : "all",
      "aggregation" : "max",
      "access" : {
        "source" : "qsfp_service"
      },
      "speed_100" : [
        [5, 24],
        [38, 26],
        [40, 28],
        [41, 30],
        [42, 32],
        [44, 34],
        [45, 36],
        [48, 38],
        [49, 40],
        [52, 44],
        [53, 46],
        [54, 50]
      ],
      "speed_200" : [
        [5, 26],
        [43, 28],
        [45, 30],
        [47, 32],
        [49, 34],
        [50, 36],
        [54, 40],
        [56, 44],
        [58, 46],
        [61, 50]
      ],
      "speed_400" : [
        [5, 36],
        [59, 40],
        [62, 42],
        [66, 46],
        [67, 48],
        [68, 50],
        [71, 52],
        [73, 55],
        [74, 60]
      ]
    }
  },
  "sensors" : {
    "SC_TH3_DIODE1_TEMP" : {
      "scale" : 1000.0,
      "access" : {
        "source" : "thrift"
      },
      "adjustment" : [
        [0,0]
      ],
      "type" : "linear_four_curves",
      "normal_up_table" : [
        [15, 24],
        [110, 100]
      ],
      "normal_down_table" : [
        [15, 24],
        [110, 100]
      ],
      "onefail_up_table" : [
        [15, 24],
        [110, 100]
      ],
      "onefail_down_table" : [
        [15, 24],
        [110, 100]
      ],
      "alarm" : {
        "alarm_major" : 105.0,
        "alarm_minor" : 90.0,
        "alarm_minor_soak" : 15
      }
    },
    "SC_TH3_DIODE2_TEMP" : {
      "scale" : 1000.0,
      "access" : {
        "source" : "thrift"
      },
      "adjustment" : [
        [0,0]
      ],
      "type" : "linear_four_curves",
      "normal_up_table" : [
        [15, 35],
        [110, 100]
      ],
      "normal_down_table" : [
        [15, 35],
        [110, 100]
      ],
      "onefail_up_table" : [
        [15, 35],
        [110, 100]
      ],
      "onefail_down_table" : [
        [15, 35],
        [110, 100]
      ],
      "alarm" : {
        "alarm_major" : 105.0,
        "alarm_minor" : 90.0,
        "alarm_minor_soak" : 15
      }
    }
  },
  "fans" : {
    "fan_1" : {
      "rpm" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan1_input"
      },
      "pwm" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/pwm1"
      },
      "pwm_range_min" : 1,
      "pwm_range_max" : 255,
      "presence" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan1_present"
      },
      "fan_present_val" : 1,
      "fan_missing_val" : 0,
      "led" : {
        "source" : "sysfs",
        "path" : "sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan1_led"
      },
      "fan_good_led_val" : 2,
      "fan_fail_led_val" : 1
    },
    "fan_2" : {
      "rpm" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan2_input"
      },
      "pwm" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/pwm2"
      },
      "pwm_range_min" : 1,
      "pwm_range_max" : 255,
      "presence" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan2_present"
      },
      "fan_present_val" : 1,
      "fan_missing_val" : 0,
      "led" : {
        "source" : "sysfs",
        "path" : "sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan2_led"
      },
      "fan_good_led_val" : 2,
      "fan_fail_led_val" : 1
    },
    "fan_3" : {
      "rpm" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan3_input"
      },
      "pwm" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/pwm3"
      },
      "pwm_range_min" : 1,
      "pwm_range_max" : 255,
      "presence" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan3_present"
      },
      "fan_present_val" : 1,
      "fan_missing_val" : 0,
      "led" : {
        "source" : "sysfs",
        "path" : "sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan3_led"
      },
      "fan_good_led_val" : 2,
      "fan_fail_led_val" : 1
    },
    "fan_4" : {
      "rpm" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan4_input"
      },
      "pwm" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/pwm4"
      },
      "pwm_range_min" : 1,
      "pwm_range_max" : 255,
      "presence" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan4_present"
      },
      "fan_present_val" : 1,
      "fan_missing_val" : 0,
      "led" : {
        "source" : "sysfs",
        "path" : "sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan4_led"
      },
      "fan_good_led_val" : 2,
      "fan_fail_led_val" : 1
    },
    "fan_5" : {
      "rpm" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan5_input"
      },
      "pwm" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/pwm5"
      },
      "pwm_range_min" : 1,
      "pwm_range_max" : 255,
      "presence" : {
        "source" : "sysfs",
        "path" : "/sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan5_present"
      },
      "fan_present_val" : 1,
      "fan_missing_val" : 0,
      "led" : {
        "source" : "sysfs",
        "path" : "sys/bus/i2c/drivers/rook-fan-cpld/17-0060/hwmon/hwmon2/fan5_led"
      },
      "fan_good_led_val" : 2,
      "fan_fail_led_val" : 1
    }
  },
  "zones": {
    "zone1" : {
      "zone_type" : "max",
      "sensors" : [
        "SC_TH3_DIODE1_TEMP",
        "SC_TH3_DIODE2_TEMP",
        "qsfp_group_1"
      ],
      "slope" : 3,
      "fans" : [
        "fan_1",
        "fan_2",
        "fan_3",
        "fan_4",
        "fan_5",
        "fan_6"
      ]
    }
  }
})";
}
} // namespace facebook::fboss::platform
