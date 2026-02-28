#include "mqtt.h"
#include "BoardConfig.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <ctype.h>
#include <mqtt_utils.h>
#include <stdio.h>
#include <string>
#include <sys/time.h>
#include <time.h>

RTC_DATA_ATTR int numMessagesPublished = 0;
static bool baseDiscoveryPublished = false;
RTC_DATA_ATTR time_t lastTimeSyncEpoch = 0;

#define MAX_MESSAGE_SIZE 1536
constexpr time_t MIN_VALID_EPOCH = 1704067200; // 2024-01-01 00:00:00 UTC

namespace {
bool epochLooksValid(time_t epoch) { return epoch >= MIN_VALID_EPOCH; }

bool syncTimeIfNeeded(time_t &now) {
    bool needsSync = !epochLooksValid(now) || lastTimeSyncEpoch == 0;
    if (!needsSync && now >= lastTimeSyncEpoch) {
        needsSync = (now - lastTimeSyncEpoch) >= MONITOR_NTP_SYNC_INTERVAL_SECONDS;
    }
    if (!needsSync && now < lastTimeSyncEpoch) {
        needsSync = true;
    }

    if (!needsSync) {
        return true;
    }

    configTzTime(MONITOR_TIMEZONE, MONITOR_NTP_SERVER);
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo, 10000)) {
        return false;
    }

    now = mktime(&timeInfo);
    if (!epochLooksValid(now)) {
        return false;
    }
    lastTimeSyncEpoch = now;
    return true;
}

void publishSensorDiscovery(PubSubClient &client, const DeviceConfig &cfg, const String &deviceId,
                            const String &baseTopic, const char *sensorKey, const char *friendlySuffix,
                            const char *unit, const char *deviceClass, const char *stateClass) {
    String sensor(sensorKey);
    String discoveryTopic = "homeassistant/sensor/" + deviceId + "/" + sensor + "/config";
    String stateTopic = baseTopic + "/" + sensor;

    StaticJsonDocument<384> doc;
    String friendlyName = cfg.deviceName + " " + friendlySuffix;
    String uniqueId = deviceId + "_" + sensor;

    doc["name"] = friendlyName;
    doc["state_topic"] = stateTopic;
    doc["unique_id"] = uniqueId;
    if (unit) {
        doc["unit_of_measurement"] = unit;
    }
    if (deviceClass) {
        doc["device_class"] = deviceClass;
    }
    if (stateClass) {
        doc["state_class"] = stateClass;
    }
    doc["device"]["identifiers"][0] = deviceId;
    doc["device"]["name"] = cfg.deviceName;
    doc["device"]["manufacturer"] = "EnvMon";
    doc["device"]["sw_version"] = cfg.fwVersion;

    char payload[384];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    if (len > 0 && len < sizeof(payload)) {
        client.publish(discoveryTopic.c_str(), reinterpret_cast<const uint8_t *>(payload), len, true);
    }
}

void publishBaseDiscovery(PubSubClient &client, const DeviceConfig &cfg, const String &deviceId,
                          const String &baseTopic) {
    publishSensorDiscovery(client, cfg, deviceId, baseTopic, "temperature", "Temperature", "°C", "temperature",
                           "measurement");
    publishSensorDiscovery(client, cfg, deviceId, baseTopic, "humidity", "Humidity", "%", "humidity", "measurement");
    publishSensorDiscovery(client, cfg, deviceId, baseTopic, "pressure", "Pressure", "hPa", "pressure", "measurement");
    publishSensorDiscovery(client, cfg, deviceId, baseTopic, "battery_voltage", "Battery Voltage", "V", "voltage",
                           "measurement");
}

} // namespace

bool publishValues(Data *data, int numValues, int timeToPublishInSeconds, const DeviceConfig &cfg) {
    WiFiClient wifiClient;
    PubSubClient client(wifiClient);
    client.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
    client.setBufferSize(MAX_MESSAGE_SIZE);

    if (!mqtt_utils::hasRequiredCredentials(cfg.mqttUser.c_str(), cfg.mqttPassword.c_str())) {
        Log.error("MQTT credentials are required; skipping publish" CR);
        return false;
    }

    bool connected = client.connect(cfg.deviceName.c_str(), cfg.mqttUser.c_str(), cfg.mqttPassword.c_str());
    if (!connected) {
        Log.error("Failed to connect to %s:%d, will retry next wake cycle" CR, cfg.mqttHost.c_str(), cfg.mqttPort);
        return false;
    }

    time_t now = time(nullptr);
    if (!syncTimeIfNeeded(now) || !epochLooksValid(now)) {
        Log.error("Failed to obtain valid time" CR);
        client.disconnect();
        return false;
    }

    if (numValues <= 0) {
        Log.error("No sensor samples available to publish" CR);
        client.disconnect();
        return false;
    }

    long timeBetweenMeasurements = timeToPublishInSeconds / numValues;
    if (timeBetweenMeasurements <= 0) {
        timeBetweenMeasurements = 1;
    }
    long timestamp = static_cast<long>(now) - ((numValues - 1) * timeBetweenMeasurements);

    std::string baseTopicStd = mqtt_utils::normalizeBaseTopic(cfg.mqttTopic.c_str());
    String baseTopic = baseTopicStd.c_str();
    std::string deviceIdStd = mqtt_utils::sanitizeIdentifier(cfg.deviceName.c_str());
    String deviceId = deviceIdStd.c_str();
    if (!baseDiscoveryPublished) {
        publishBaseDiscovery(client, cfg, deviceId, baseTopic);
        baseDiscoveryPublished = true;
    }

    char json[MAX_MESSAGE_SIZE];
    StaticJsonDocument<MAX_MESSAGE_SIZE> doc;

    doc["name"] = cfg.deviceName;
    doc["ver"] = cfg.fwVersion;
    doc["seq"] = numMessagesPublished++;

    for (int i = 0; i < numValues; i++) {
        doc["data"][i]["epoch"] = timestamp;
        doc["data"][i]["temp"] = data[i].temperature;
        doc["data"][i]["hum"] = data[i].humidity;
        doc["data"][i]["pres"] = data[i].pressure;
        doc["data"][i]["bat"] = data[i].batteryVoltage;
        timestamp += timeBetweenMeasurements;
    }

    size_t size = serializeJson(doc, json, MAX_MESSAGE_SIZE);

    if (!client.publish(cfg.mqttTopic.c_str(), json, size)) {
        Log.error("Failed to publish!" CR);
        client.disconnect();
        return false;
    }
    Log.notice("Published %s to %s:%d" CR, json, cfg.mqttHost.c_str(), cfg.mqttPort);

    const Data &latest = data[numValues - 1];
    struct SensorPublish {
        const char *key;
        float value;
        int decimals;
    };
    SensorPublish sensors[] = {{"temperature", latest.temperature, 2},
                               {"humidity", latest.humidity, 1},
                               {"pressure", latest.pressure, 2},
                               {"battery_voltage", latest.batteryVoltage, 3}};

    const size_t sensorCount = sizeof(sensors) / sizeof(SensorPublish);

    for (size_t i = 0; i < sensorCount; i++) {
        const auto &sensor = sensors[i];
        char valueBuffer[16];
        snprintf(valueBuffer, sizeof(valueBuffer), "%.*f", sensor.decimals, sensor.value);
        String topic = baseTopic + "/" + sensor.key;
        if (!client.publish(topic.c_str(), valueBuffer, true)) {
            Log.warning("Failed to publish %s to %s" CR, sensor.key, topic.c_str());
        }
    }

    client.disconnect();
    return true;
}
