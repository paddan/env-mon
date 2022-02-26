#ifndef _DEVICE_CONFIG_H_
#define _DEVICE_CONFIG_H_
#include <Arduino.h>

class DeviceConfig {
  public:
    String deviceName = "EnvMon";
    String mqttHost = "toadstation.local";
    int mqttPort = 1883;
    String mqttTopic = "env-mon/data";
    int fwVersion = 0;

    DeviceConfig();
    void save();
};

#endif
