#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <DeviceConfig.h>
#include <SPIFFS.h>

#define MAX_MESSAGE_SIZE 600

DeviceConfig::DeviceConfig() {
    if (!SPIFFS.begin(true)) {
        Log.error("Failed to mount SPIFFS; using in-memory defaults" CR);
        loadedFromStorage = false;
        resetToDefaults(false);
        return;
    }
    File file = SPIFFS.open("/deviceConfig.json", "r");
    bool loaded = false;

    if (file) {
        StaticJsonDocument<MAX_MESSAGE_SIZE> doc;
        DeserializationError err = deserializeJson(doc, file);
        if (!err) {
            JsonObject obj = doc.as<JsonObject>();

            String _deviceName = obj["deviceName"].as<String>();
            int _fwVersion = obj["fwVersion"];
            String _mqttHost = obj["mqttHost"].as<String>();
            int _mqttPort = obj["mqttPort"];
            String _mqttTopic = obj["mqttTopic"].as<String>();
            String _mqttUser = obj["mqttUser"].as<String>();
            String _mqttPassword = obj["mqttPassword"].as<String>();
            int _measureInterval = obj["measureInterval"];
            int _publishInterval = obj["publishInterval"];

            if (!_deviceName.isEmpty())
                deviceName = _deviceName;
            if (!_mqttHost.isEmpty())
                mqttHost = _mqttHost;
            if (_mqttPort != 0)
                mqttPort = _mqttPort;
            if (!_mqttTopic.isEmpty())
                mqttTopic = _mqttTopic;
            if (!_mqttUser.isEmpty())
                mqttUser = _mqttUser;
            if (!_mqttPassword.isEmpty())
                mqttPassword = _mqttPassword;
            if (_measureInterval > 0)
                measureIntervalSeconds = _measureInterval;
            if (_publishInterval > 0)
                publishIntervalSeconds = _publishInterval;
            fwVersion = _fwVersion;

            loaded = true;
        } else {
            Log.warning("Failed to parse deviceConfig.json (%s), restoring defaults." CR, err.c_str());
        }
    } else {
        Log.warning("deviceConfig.json missing, restoring defaults." CR);
    }

    if (file) {
        file.close();
    }
    SPIFFS.end();

    loadedFromStorage = loaded;

    if (!loaded) {
        resetToDefaults(false);
        save();
        return;
    }

    normalizeIntervals();
}

void DeviceConfig::save() {
    normalizeIntervals();

    if (!SPIFFS.begin(true)) {
        Log.error("Failed to mount SPIFFS for save; keeping in-memory config" CR);
        return;
    }
    File file = SPIFFS.open("/deviceConfig.json", "w");
    if (!file) {
        Log.error("Failed to open /deviceConfig.json for writing" CR);
        SPIFFS.end();
        return;
    }

    StaticJsonDocument<MAX_MESSAGE_SIZE> doc;

    doc["deviceName"] = deviceName;
    doc["fwVersion"] = fwVersion;
    doc["mqttHost"] = mqttHost;
    doc["mqttPort"] = mqttPort;
    doc["mqttTopic"] = mqttTopic;
    doc["mqttUser"] = mqttUser;
    doc["mqttPassword"] = mqttPassword;
    doc["measureInterval"] = measureIntervalSeconds;
    doc["publishInterval"] = publishIntervalSeconds;

    if (serializeJson(doc, file) == 0) {
        Log.error("Failed to serialize config JSON" CR);
    }

    file.flush();
    file.close();
    SPIFFS.end();
}

void DeviceConfig::resetToDefaults(bool persist) {
    deviceName = MONITOR_DEFAULT_DEVICE_NAME;
    mqttHost = MONITOR_DEFAULT_MQTT_HOST;
    mqttPort = MONITOR_DEFAULT_MQTT_PORT;
    mqttTopic = MONITOR_DEFAULT_MQTT_TOPIC;
    mqttUser = MONITOR_DEFAULT_MQTT_USER;
    mqttPassword = MONITOR_DEFAULT_MQTT_PASSWORD;
    measureIntervalSeconds = MONITOR_DEFAULT_MEASURE_INTERVAL_SECONDS;
    publishIntervalSeconds = MONITOR_DEFAULT_PUBLISH_INTERVAL_SECONDS;
    fwVersion = 0;

    normalizeIntervals();

    if (persist) {
        save();
    }
}

void DeviceConfig::normalizeIntervals() {
    if (mqttUser.isEmpty()) {
        mqttUser = MONITOR_DEFAULT_MQTT_USER;
    }
    if (mqttPassword.isEmpty()) {
        mqttPassword = MONITOR_DEFAULT_MQTT_PASSWORD;
    }
    if (measureIntervalSeconds <= 0) {
        measureIntervalSeconds = MONITOR_DEFAULT_MEASURE_INTERVAL_SECONDS;
    }
    if (publishIntervalSeconds < measureIntervalSeconds) {
        publishIntervalSeconds = measureIntervalSeconds;
    }
}
