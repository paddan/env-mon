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
constexpr wifi_power_t TX_POWER_LEVELS[] = {WIFI_POWER_2dBm,   WIFI_POWER_5dBm,  WIFI_POWER_7dBm,
                                            WIFI_POWER_8_5dBm, WIFI_POWER_11dBm, WIFI_POWER_13dBm,
                                            WIFI_POWER_15dBm,  WIFI_POWER_17dBm, WIFI_POWER_19dBm,
                                            WIFI_POWER_19_5dBm};
constexpr int8_t DEFAULT_TX_POWER_INDEX = 2; // WIFI_POWER_7dBm
constexpr long STRONG_RSSI_THRESHOLD_DBM = -65;
constexpr uint8_t STABLE_SUCCESSES_FOR_STEP_DOWN = 6;

RTC_DATA_ATTR int8_t txPowerIndex = DEFAULT_TX_POWER_INDEX;
RTC_DATA_ATTR uint8_t strongLinkSuccesses = 0;

int8_t maxTxPowerIndex() {
    return static_cast<int8_t>((sizeof(TX_POWER_LEVELS) / sizeof(TX_POWER_LEVELS[0])) - 1);
}

void clampTxPowerIndex() {
    if (txPowerIndex < 0) {
        txPowerIndex = 0;
    } else if (txPowerIndex > maxTxPowerIndex()) {
        txPowerIndex = maxTxPowerIndex();
    }
}

const char *txPowerLabel(wifi_power_t power) {
    switch (power) {
    case WIFI_POWER_19_5dBm:
        return "19.5";
    case WIFI_POWER_19dBm:
        return "19";
    case WIFI_POWER_18_5dBm:
        return "18.5";
    case WIFI_POWER_17dBm:
        return "17";
    case WIFI_POWER_15dBm:
        return "15";
    case WIFI_POWER_13dBm:
        return "13";
    case WIFI_POWER_11dBm:
        return "11";
    case WIFI_POWER_8_5dBm:
        return "8.5";
    case WIFI_POWER_7dBm:
        return "7";
    case WIFI_POWER_5dBm:
        return "5";
    case WIFI_POWER_2dBm:
        return "2";
    case WIFI_POWER_MINUS_1dBm:
        return "-1";
    default:
        return "unknown";
    }
}

bool applyCurrentTxPower() {
    clampTxPowerIndex();
    const wifi_power_t power = TX_POWER_LEVELS[txPowerIndex];
    if (!WiFi.setTxPower(power)) {
        Log.warning("Failed to set WiFi TX power to %s dBm" CR, txPowerLabel(power));
        return false;
    }
    return true;
}

float txPowerDbm(wifi_power_t power) { return static_cast<float>(static_cast<int>(power)) / 4.0f; }

float currentTxPowerDbm() { return txPowerDbm(WiFi.getTxPower()); }

void logWifiSignal(const char *context) {
    if (!WiFi.isConnected()) {
        return;
    }

    const String ssid = WiFi.SSID();
    const long rssi = WiFi.RSSI();
    const long channel = static_cast<long>(WiFi.channel());
    const float txPower = currentTxPowerDbm();
    Log.notice("%s SSID '%s', RSSI %ld dBm, TX %.1f dBm, channel %ld" CR, context, ssid.c_str(), rssi, txPower,
               channel);
}

void tuneTxPowerAfterSuccess() {
    if (!WiFi.isConnected()) {
        strongLinkSuccesses = 0;
        return;
    }

    const long rssi = WiFi.RSSI();
    if (rssi >= STRONG_RSSI_THRESHOLD_DBM) {
        if (strongLinkSuccesses < 255) {
            strongLinkSuccesses++;
        }
    } else {
        strongLinkSuccesses = 0;
    }

    if (strongLinkSuccesses < STABLE_SUCCESSES_FOR_STEP_DOWN || txPowerIndex <= 0) {
        return;
    }

    const wifi_power_t previous = TX_POWER_LEVELS[txPowerIndex];
    txPowerIndex--;
    strongLinkSuccesses = 0;
    if (applyCurrentTxPower()) {
        const wifi_power_t current = TX_POWER_LEVELS[txPowerIndex];
        Log.notice("Auto-tuned TX power down: %s -> %s dBm after stable RSSI %ld dBm" CR, txPowerLabel(previous),
                   txPowerLabel(current), rssi);
    }
}

void stepUpTxPowerAfterFailure(int8_t failedIndex) {
    if (failedIndex >= maxTxPowerIndex()) {
        return;
    }

    const wifi_power_t failed = TX_POWER_LEVELS[failedIndex];
    txPowerIndex = failedIndex + 1;
    strongLinkSuccesses = 0;
    const wifi_power_t next = TX_POWER_LEVELS[txPowerIndex];
    Log.warning("WiFi connect timeout at %s dBm, increasing TX power to %s dBm" CR, txPowerLabel(failed),
                txPowerLabel(next));
}

bool tryFastReconnect() {
    const String storedSsid = WiFi.SSID();
    if (storedSsid.isEmpty()) {
        return false;
    }

    clampTxPowerIndex();
    for (int8_t attemptIndex = txPowerIndex; attemptIndex <= maxTxPowerIndex(); ++attemptIndex) {
        txPowerIndex = attemptIndex;
        applyCurrentTxPower();

        WiFi.begin();
        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if ((millis() - start) >= MONITOR_WIFI_FAST_CONNECT_TIMEOUT_MS) {
                break;
            }
            delay(50);
        }

        if (WiFi.status() == WL_CONNECTED) {
            logWifiSignal("Fast WiFi reconnect succeeded:");
            tuneTxPowerAfterSuccess();
            return true;
        }

        stepUpTxPowerAfterFailure(attemptIndex);
    }

    return false;
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
    save_config = false;

    if (WiFi.isConnected()) {
        tuneTxPowerAfterSuccess();
        logWifiSignal("WiFi already connected:");
        return;
    }

    WiFi.mode(WIFI_STA);
    applyCurrentTxPower();

    WiFiManager wifiManager;

    if (!forcePortal && tryFastReconnect()) {
        return;
    }

    WiFi.mode(WIFI_STA);
    applyCurrentTxPower();
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

    tuneTxPowerAfterSuccess();
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
