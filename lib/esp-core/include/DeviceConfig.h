#ifndef _DEVICE_CONFIG_H_
#define _DEVICE_CONFIG_H_
#include <Arduino.h>

constexpr const char *MONITOR_DEFAULT_DEVICE_NAME = "EnvMon";
constexpr const char *MONITOR_DEFAULT_MQTT_HOST = "homeassistant.local";
constexpr int MONITOR_DEFAULT_MQTT_PORT = 1883;
constexpr const char *MONITOR_DEFAULT_MQTT_TOPIC = "env-mon/data";
constexpr const char *MONITOR_DEFAULT_MQTT_USER = "mqtt";
constexpr const char *MONITOR_DEFAULT_MQTT_PASSWORD = "mqtt";
constexpr int MONITOR_DEFAULT_MEASURE_INTERVAL_SECONDS = 3600;     // Every hour
constexpr int MONITOR_DEFAULT_PUBLISH_INTERVAL_SECONDS = 3600 * 2; // Every second hour

class DeviceConfig {
  public:
    bool loadedFromStorage = false;

    String deviceName = MONITOR_DEFAULT_DEVICE_NAME;
    String mqttHost = MONITOR_DEFAULT_MQTT_HOST;
    int mqttPort = MONITOR_DEFAULT_MQTT_PORT;
    String mqttTopic = MONITOR_DEFAULT_MQTT_TOPIC;
    String mqttUser = MONITOR_DEFAULT_MQTT_USER;
    String mqttPassword = MONITOR_DEFAULT_MQTT_PASSWORD;
    int measureIntervalSeconds = MONITOR_DEFAULT_MEASURE_INTERVAL_SECONDS;
    int publishIntervalSeconds = MONITOR_DEFAULT_PUBLISH_INTERVAL_SECONDS;
    int fwVersion = 0;

    DeviceConfig();
    void save();
    void resetToDefaults(bool persist = true);
    void normalizeIntervals();
};

#endif
