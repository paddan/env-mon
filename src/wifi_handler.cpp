#include "wifi_handler.h"
#include <Arduino.h>
#include <ArduinoLog.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <inttypes.h>

WiFiManager wifiManager; // shared so resetWifi() can clear the same instance used by the portal

bool save_config = false;

void saveConfigCallback() { save_config = true; }

void resetWifi() {
    wifiManager.resetSettings();
    DeviceConfig cfg;
    cfg.resetToDefaults();
    ESP.restart();
}

void connectToRouter(DeviceConfig &cfg) {
    if (WiFi.isConnected()) {
        return;
    }

    WiFi.mode(WIFI_STA);
    String mqttPortStr = String(cfg.mqttPort);
    String measureIntervalStr = String(cfg.measureIntervalSeconds);
    String publishIntervalStr = String(cfg.publishIntervalSeconds);
    String updateIntervalStr = String(cfg.updateIntervalSeconds);

    WiFiManagerParameter device_name("Device", "name:", cfg.deviceName.c_str(), 100);
    WiFiManagerParameter mqtt_host("MQTTHost", "host:", cfg.mqttHost.c_str(), 100);
    WiFiManagerParameter mqtt_port("MQTTPort", "port:", mqttPortStr.c_str(), 6);
    WiFiManagerParameter mqtt_topic("MQTTTopic", "topic:", cfg.mqttTopic.c_str(), 100);
    WiFiManagerParameter mqtt_user("MQTTUser", "username:", cfg.mqttUser.c_str(), 64);
    WiFiManagerParameter mqtt_password("MQTTPwd", "password:", cfg.mqttPassword.c_str(), 64);
    WiFiManagerParameter measure_interval("MeasureInterval", "measure interval (s):", measureIntervalStr.c_str(), 8);
    WiFiManagerParameter publish_interval("PublishInterval", "publish interval (s):", publishIntervalStr.c_str(), 8);
    WiFiManagerParameter update_interval("UpdateInterval", "update interval (s):", updateIntervalStr.c_str(), 10);

    wifiManager.addParameter(&device_name);
    wifiManager.addParameter(&mqtt_host);
    wifiManager.addParameter(&mqtt_port);
    wifiManager.addParameter(&mqtt_topic);
    wifiManager.addParameter(&mqtt_user);
    wifiManager.addParameter(&mqtt_password);
    wifiManager.addParameter(&measure_interval);
    wifiManager.addParameter(&publish_interval);
    wifiManager.addParameter(&update_interval);

    wifiManager.setConfigPortalTimeout(60);
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.setDarkMode(true);

    char apName[100];
    sprintf(apName, "EnvMon-%" PRIx64, ESP.getEfuseMac());
    if (!wifiManager.autoConnect(apName)) {
        Log.error("Failed to connect to %s or hit timeout, resetting wifi!" CR, apName);
        ESP.restart();
    }

    if (save_config) {
        cfg.deviceName = String(device_name.getValue());
        cfg.mqttHost = String(mqtt_host.getValue());
        cfg.mqttPort = atoi(mqtt_port.getValue());
        cfg.mqttTopic = String(mqtt_topic.getValue());
        cfg.mqttUser = String(mqtt_user.getValue());
        cfg.mqttPassword = String(mqtt_password.getValue());
        cfg.measureIntervalSeconds = atoi(measure_interval.getValue());
        cfg.publishIntervalSeconds = atoi(publish_interval.getValue());
        cfg.updateIntervalSeconds = atoi(update_interval.getValue());

        cfg.normalizeIntervals();

        cfg.save();

        save_config = false;
    }

    Log.notice("Connected %s to %s" CR, cfg.deviceName.c_str(), wifiManager.getWiFiSSID(true).c_str());
}
