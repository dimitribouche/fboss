include "common/fb303/if/fb303.thrift"
include "fboss/agent/if/ctrl.thrift"
include "fboss/agent/if/fboss.thrift"

namespace cpp2 facebook.fboss.sensor_service

struct SensorDataThrift {
  1: string name;
  2: float value;
  3: i64 timeStamp;
}

struct SensorReadRequestThrift {
  1: string label;
  2: list<string> optionalSensorList;
}

struct SensorReadResponseThrift {
  1: string requestLabel;
  2: i64 timeStamp;
  3: list<SensorDataThrift> sensorData;
}

service SensorService extends fb303.FacebookService {
  SensorReadResponseThrift getSensorValues(
    1: SensorReadRequestThrift request,
  ) throws (1: fboss.FbossBaseError error);
}