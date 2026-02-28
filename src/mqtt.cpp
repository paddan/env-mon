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

static bool baseDiscoveryPublished = false;

#define MAX_MESSAGE_SIZE 1536

namespace {
bool endsWithDeviceSegment(const std::string &topic, const std::string &deviceSegment) {
    if (deviceSegment.empty()) {
        return false;
    }
    if (topic == deviceSegment) {
        return true;
    }
    const size_t required = deviceSegment.size() + 1;
    if (topic.size() < required) {
        return false;
    }
    const size_t start = topic.size() - deviceSegment.size();
    return topic[start - 1] == '/' && topic.compare(start, deviceSegment.size(), deviceSegment) == 0;
}

float currentTxPowerDbm() { return static_cast<float>(static_cast<int>(WiFi.getTxPower())) / 4.0f; }

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
    publishSensorDiscovery(client, cfg, deviceId, baseTopic, "rssi", "WiFi RSSI", "dBm", "signal_strength",
                           "measurement");
    publishSensorDiscovery(client, cfg, deviceId, baseTopic, "tx_power", "WiFi TX Power", "dBm", nullptr,
                           "measurement");
}

} // namespace

bool publishValues(Data *data, int numValues, int timeToPublishInSeconds, const DeviceConfig &cfg) {
    (void)timeToPublishInSeconds;

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

    if (numValues <= 0) {
        Log.error("No sensor samples available to publish" CR);
        client.disconnect();
        return false;
    }

    double sumTemp = 0.0;
    double sumHum = 0.0;
    double sumPres = 0.0;
    double sumBat = 0.0;
    for (int i = 0; i < numValues; i++) {
        sumTemp += data[i].temperature;
        sumHum += data[i].humidity;
        sumPres += data[i].pressure;
        sumBat += data[i].batteryVoltage;
    }

    const float avgTemp = static_cast<float>(sumTemp / numValues);
    const float avgHum = static_cast<float>(sumHum / numValues);
    const float avgPres = static_cast<float>(sumPres / numValues);
    const float avgBat = static_cast<float>(sumBat / numValues);
    const float currentRssi = static_cast<float>(WiFi.RSSI());
    const float txPower = currentTxPowerDbm();

    std::string deviceIdStd = mqtt_utils::sanitizeIdentifier(cfg.deviceName.c_str());
    std::string baseTopicStd = mqtt_utils::normalizeBaseTopic(cfg.mqttTopic.c_str());
    if (!endsWithDeviceSegment(baseTopicStd, deviceIdStd)) {
        baseTopicStd += "/";
        baseTopicStd += deviceIdStd;
    }
    String baseTopic = baseTopicStd.c_str();
    String deviceId = deviceIdStd.c_str();
    if (!baseDiscoveryPublished) {
        publishBaseDiscovery(client, cfg, deviceId, baseTopic);
        baseDiscoveryPublished = true;
    }

    Log.notice("Publishing averaged values from %d samples to base topic '%s' on %s:%d" CR, numValues,
               baseTopic.c_str(), cfg.mqttHost.c_str(), cfg.mqttPort);

    struct SensorPublish {
        const char *key;
        float value;
        int decimals;
    };
    SensorPublish sensors[] = {{"temperature", avgTemp, 2},
                               {"humidity", avgHum, 1},
                               {"pressure", avgPres, 2},
                               {"battery_voltage", avgBat, 3},
                               {"rssi", currentRssi, 0},
                               {"tx_power", txPower, 1}};

    const size_t sensorCount = sizeof(sensors) / sizeof(SensorPublish);

    for (size_t i = 0; i < sensorCount; i++) {
        const auto &sensor = sensors[i];
        char valueBuffer[16];
        snprintf(valueBuffer, sizeof(valueBuffer), "%.*f", sensor.decimals, sensor.value);
        String topic = baseTopic + "/" + sensor.key;
        if (!client.publish(topic.c_str(), valueBuffer, true)) {
            Log.warning("Failed to publish %s to %s" CR, sensor.key, topic.c_str());
        } else {
            Log.notice("Published %s to topic '%s'" CR, sensor.key, topic.c_str());
        }
    }

    client.disconnect();
    return true;
}
