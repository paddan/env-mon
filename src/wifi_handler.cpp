#include "wifi_handler.h"
#include <Arduino.h>
#include <ArduinoLog.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <inttypes.h>

WiFiManager wifiManager;

bool save_config = false;

void saveConfigCallback() { save_config = true; }

void resetWifi() {
    wifiManager.resetSettings();
    ESP.restart();
}

void connectToRouter(DeviceConfig cfg) {
    if (WiFi.isConnected()) {
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFiManager wifiManager;

    WiFiManagerParameter device_name("Device", "name:", cfg.deviceName.c_str(), 100);
    WiFiManagerParameter mqtt_host("MQTTHost", "host:", cfg.mqttHost.c_str(), 100);
    WiFiManagerParameter mqtt_port("MQTTPort", "port:", String(cfg.mqttPort).c_str(), 5);
    WiFiManagerParameter mqtt_topic("MQTTTopic", "topic:", cfg.mqttTopic.c_str(), 100);

    wifiManager.addParameter(&device_name);
    wifiManager.addParameter(&mqtt_host);
    wifiManager.addParameter(&mqtt_port);
    wifiManager.addParameter(&mqtt_topic);

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

        cfg.save();

        save_config = false;
    }

    Log.notice("Connected %s to %s" CR, cfg.deviceName.c_str(), wifiManager.getWiFiSSID(true).c_str());
}
