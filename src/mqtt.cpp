#include "mqtt.h"
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
static bool pmDiscoveryPublished = false;

#define MAX_MESSAGE_SIZE 1536
#define NTP_SERVER "europe.pool.ntp.org"

namespace {
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

void publishPmDiscovery(PubSubClient &client, const DeviceConfig &cfg, const String &deviceId, const String &baseTopic) {
    publishSensorDiscovery(client, cfg, deviceId, baseTopic, "pm1_0", "PM1.0", "µg/m³", "pm1", "measurement");
    publishSensorDiscovery(client, cfg, deviceId, baseTopic, "pm2_5", "PM2.5", "µg/m³", "pm25", "measurement");
    publishSensorDiscovery(client, cfg, deviceId, baseTopic, "pm10", "PM10", "µg/m³", "pm10", "measurement");
}
} // namespace

void publishValues(Data *data, int numValues, int timeToPublishInSeconds, const DeviceConfig &cfg) {
    WiFiClient wifiClient;
    PubSubClient client(wifiClient);
    client.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
    client.setBufferSize(MAX_MESSAGE_SIZE);

    const bool hasMqttAuth = cfg.mqttUser.length() > 0;
    bool connected = false;
    if (hasMqttAuth) {
        connected = client.connect(cfg.deviceName.c_str(), cfg.mqttUser.c_str(), cfg.mqttPassword.c_str());
    } else {
        connected = client.connect(cfg.deviceName.c_str());
    }

    if (!connected) {
        Log.error("Failed to connect to %s:%d, will retry next wake cycle" CR, cfg.mqttHost.c_str(), cfg.mqttPort);
        return;
    }

    configTime(3600, 3600, NTP_SERVER);
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo)) {
        Log.error("Failed to obtain time" CR);
        return;
    }

    if (numValues <= 0) {
        Log.error("No sensor samples available to publish" CR);
        return;
    }

    long now = mktime(&timeInfo);
    long timeBetweenMeasurements = timeToPublishInSeconds / numValues;
    if (timeBetweenMeasurements <= 0) {
        timeBetweenMeasurements = 1;
    }
    long timestamp = now - ((numValues - 1) * timeBetweenMeasurements);

    std::string baseTopicStd = mqtt_utils::normalizeBaseTopic(cfg.mqttTopic.c_str());
    String baseTopic = baseTopicStd.c_str();
    std::string deviceIdStd = mqtt_utils::sanitizeIdentifier(cfg.deviceName.c_str());
    String deviceId = deviceIdStd.c_str();
    bool hasPmData = false;
    for (int i = 0; i < numValues; i++) {
        if (data[i].hasPm) {
            hasPmData = true;
            break;
        }
    }
    if (!baseDiscoveryPublished) {
        publishBaseDiscovery(client, cfg, deviceId, baseTopic);
        baseDiscoveryPublished = true;
    }
    if (hasPmData && !pmDiscoveryPublished) {
        publishPmDiscovery(client, cfg, deviceId, baseTopic);
        pmDiscoveryPublished = true;
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
        if (data[i].hasPm) {
            doc["data"][i]["pm1"] = data[i].pm1_0;
            doc["data"][i]["pm25"] = data[i].pm2_5;
            doc["data"][i]["pm10"] = data[i].pm10;
        }
        timestamp += timeBetweenMeasurements;
    }

    size_t size = serializeJson(doc, json, MAX_MESSAGE_SIZE);

    if (!client.publish(cfg.mqttTopic.c_str(), json, size)) {
        Log.error("Failed to publish!" CR);
        return;
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
                               {"battery_voltage", latest.batteryVoltage, 3},
                               {"pm1_0", latest.pm1_0, 0},
                               {"pm2_5", latest.pm2_5, 0},
                               {"pm10", latest.pm10, 0}};

    const size_t sensorCount = latest.hasPm ? (sizeof(sensors) / sizeof(SensorPublish))
                                            : (sizeof(sensors) / sizeof(SensorPublish) - 3);

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
}
