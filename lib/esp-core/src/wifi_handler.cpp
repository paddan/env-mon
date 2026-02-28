#include "wifi_handler.h"
#include "BoardConfig.h"
#include <Arduino.h>
#include <ArduinoLog.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <inttypes.h>

bool save_config = false;

void saveConfigCallback() { save_config = true; }

namespace {
void logWifiSignal(const char *context) {
    if (!WiFi.isConnected()) {
        return;
    }

    const String ssid = WiFi.SSID();
    const long rssi = WiFi.RSSI();
    const long channel = static_cast<long>(WiFi.channel());
    Log.notice("%s SSID '%s', RSSI %ld dBm, channel %ld" CR, context, ssid.c_str(), rssi, channel);
}

bool tryFastReconnect() {
    const String storedSsid = WiFi.SSID();
    if (storedSsid.isEmpty()) {
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin();
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if ((millis() - start) >= MONITOR_WIFI_FAST_CONNECT_TIMEOUT_MS) {
            WiFi.disconnect(false, true);
            return false;
        }
        delay(50);
    }

    logWifiSignal("Fast WiFi reconnect succeeded:");
    return true;
}
} // namespace

void resetWifi() {
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    WiFi.disconnect(true, true);
    DeviceConfig cfg;
    cfg.resetToDefaults();
    Log.warning("WiFi reset requested; starting WiFiManager portal" CR);
    connectToRouter(cfg, true);
}

void connectToRouter(DeviceConfig &cfg, bool forcePortal) {
    WiFiManager wifiManager;
    save_config = false;

    if (WiFi.isConnected()) {
        logWifiSignal("WiFi already connected:");
        return;
    }

    if (!forcePortal && tryFastReconnect()) {
        return;
    }

    WiFi.mode(WIFI_STA);
    String mqttPortStr = String(cfg.mqttPort);
    String measureIntervalStr = String(cfg.measureIntervalSeconds);
    String publishIntervalStr = String(cfg.publishIntervalSeconds);

    WiFiManagerParameter device_name("Device", "name:", cfg.deviceName.c_str(), 100);
    WiFiManagerParameter mqtt_host("MQTTHost", "host:", cfg.mqttHost.c_str(), 100);
    WiFiManagerParameter mqtt_port("MQTTPort", "port:", mqttPortStr.c_str(), 6);
    WiFiManagerParameter mqtt_topic("MQTTTopic", "topic:", cfg.mqttTopic.c_str(), 100);
    WiFiManagerParameter mqtt_user("MQTTUser", "username:", cfg.mqttUser.c_str(), 64);
    WiFiManagerParameter mqtt_password("MQTTPwd", "password:", cfg.mqttPassword.c_str(), 64);
    WiFiManagerParameter measure_interval("MeasureInterval", "measure interval (s):", measureIntervalStr.c_str(), 8);
    WiFiManagerParameter publish_interval("PublishInterval", "publish interval (s):", publishIntervalStr.c_str(), 8);

    wifiManager.addParameter(&device_name);
    wifiManager.addParameter(&mqtt_host);
    wifiManager.addParameter(&mqtt_port);
    wifiManager.addParameter(&mqtt_topic);
    wifiManager.addParameter(&mqtt_user);
    wifiManager.addParameter(&mqtt_password);
    wifiManager.addParameter(&measure_interval);
    wifiManager.addParameter(&publish_interval);

    constexpr uint32_t portalTimeoutMs = 120000;

    wifiManager.setConfigPortalBlocking(false);
    wifiManager.setConfigPortalTimeout(portalTimeoutMs / 1000);
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.setDarkMode(true);

    char apName[100];
    snprintf(apName, sizeof(apName), "EnvMon-%" PRIx64, ESP.getEfuseMac());
    bool connected = wifiManager.autoConnect(apName);
    if (!connected) {
        const uint32_t start = millis();
        while (!WiFi.isConnected()) {
            if (wifiManager.process()) {
                connected = true;
                break;
            }

            if ((millis() - start) >= portalTimeoutMs) {
                break;
            }
            delay(25);
        }
    }

    if (!connected && !WiFi.isConnected()) {
        Log.error("Failed to connect to %s or hit timeout (%lu ms), restarting" CR, apName,
                  static_cast<unsigned long>(portalTimeoutMs));
        ESP.restart();
    }

    logWifiSignal("WiFi connected:");

    if (save_config) {
        cfg.deviceName = String(device_name.getValue());
        cfg.mqttHost = String(mqtt_host.getValue());
        cfg.mqttPort = atoi(mqtt_port.getValue());
        cfg.mqttTopic = String(mqtt_topic.getValue());
        cfg.mqttUser = String(mqtt_user.getValue());
        cfg.mqttPassword = String(mqtt_password.getValue());
        cfg.measureIntervalSeconds = atoi(measure_interval.getValue());
        cfg.publishIntervalSeconds = atoi(publish_interval.getValue());

        cfg.normalizeIntervals();

        cfg.save();

        save_config = false;
        Log.notice("Configuration saved, restarting device" CR);
        delay(200);
        ESP.restart();
    }
}
