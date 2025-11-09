#include "BoardConfig.h"
#include "battery.h"
#include "bme280.h"
#include "driver/adc.h"
#include "mqtt.h"
#include "server_ota.h"
#include "wifi_handler.h"
#include <Arduino.h>
#include <ArduinoLog.h>
#include <DeviceConfig.h>
#include <PubSubClient.h>
#include <esp_wifi.h>
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

#include <stdio.h>
#include <time.h>

#if defined(CONFIG_IDF_TARGET_ESP32)
#include <esp_bt.h>
#include <esp_bt_main.h>
#endif

#define FW_IMAGE_URL "http://toadstation.local:6060/esp32firmware/environment-monitor/firmware.bin"
#define VERSION_URL "http://toadstation.local:6060/esp32firmware/environment-monitor/version.txt"

#define uS_TO_S_FACTOR 1000000LLU /* Conversion factor for micro seconds to seconds */
#define RESET_HOLD_MS 2000        /* Hold reset button for 2s to wipe config */
#define RESET_POLL_INTERVAL_MS 50
constexpr int USB_MEASURE_INTERVAL_SECONDS = 10;
constexpr int USB_PUBLISH_INTERVAL_SECONDS = 60;

constexpr int MAX_MEASUREMENTS = 120;

RTC_DATA_ATTR int publishCount = 0;
RTC_DATA_ATTR int updateCount = 0;
RTC_DATA_ATTR Data data[MAX_MEASUREMENTS];

namespace {
struct TimingConfig {
    int measureInterval;
    int publishInterval;
    int updateInterval;
    int samplesPerPublish;
    bool publishClamped;
};

TimingConfig computeTimingConfig(const DeviceConfig &cfg) {
    TimingConfig timing{};
    timing.measureInterval = cfg.measureIntervalSeconds;
    timing.publishInterval = cfg.publishIntervalSeconds;
    timing.updateInterval = cfg.updateIntervalSeconds;
    timing.samplesPerPublish = (timing.publishInterval + timing.measureInterval - 1) / timing.measureInterval;
    if (timing.samplesPerPublish < 1) {
        timing.samplesPerPublish = 1;
    }
    timing.publishClamped = false;
    if (timing.samplesPerPublish > MAX_MEASUREMENTS) {
        timing.samplesPerPublish = MAX_MEASUREMENTS;
        timing.publishClamped = true;
    }
    return timing;
}

bool resetButtonHeld(uint32_t holdMs = RESET_HOLD_MS) {
    if (digitalRead(MONITOR_RESET_PIN) == LOW) {
        return false;
    }

    uint32_t elapsed = 0;
    while (elapsed < holdMs) {
        if (digitalRead(MONITOR_RESET_PIN) == LOW) {
            return false;
        }
        delay(RESET_POLL_INTERVAL_MS);
        elapsed += RESET_POLL_INTERVAL_MS;
    }
    return true;
}

void handleResetRequest() {
    if (resetButtonHeld()) {
        Log.warning("Reset button held, restoring default configuration" CR);
        resetWifi();
    }
}

bool isUsbConnected() {
#if defined(ARDUINO_USB_CDC_ON_BOOT) || defined(ARDUINO_USB_MODE)
    return Serial;
#else
    return Serial.dtr() || Serial.rts();
#endif
}

void logLatestMeasurement(const Data &measurement, bool loggingEnabled) {
    if (!loggingEnabled) {
        return;
    }

    time_t now = time(nullptr);
    struct tm timeInfo;
    bool hasTime = localtime_r(&now, &timeInfo);
    char timestamp[32];
    if (hasTime) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeInfo);
    } else {
        strncpy(timestamp, "unknown", sizeof(timestamp));
        timestamp[sizeof(timestamp) - 1] = '\0';
    }

    char msg[160];
    snprintf(msg, sizeof(msg), "[%s | %ld] Measurement: %.2f°C, %.1f%%, %.2f hPa, %.3f V", timestamp,
             static_cast<long>(now), measurement.temperature, measurement.humidity, measurement.pressure,
             measurement.batteryVoltage);
    Log.notice("%s" CR, msg);
}
} // namespace

void setup() {
    Serial.begin(115200);
    pinMode(MONITOR_RESET_PIN, INPUT_PULLDOWN);

    bool usbConnected = isUsbConnected();
    int logLevel = usbConnected ? LOG_LEVEL_VERBOSE : LOG_LEVEL_SILENT;
    Log.begin(logLevel, &Serial);
    if (usbConnected) {
        Log.notice("USB connection detected; forcing 10s measure / 60s publish intervals and disabling OTA updates" CR);
    }

#if defined(CONFIG_IDF_TARGET_ESP32)
    esp_bluedroid_disable();
    esp_bt_controller_disable();
#endif

    handleResetRequest();

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        Log.warning("Resetting wifi!" CR);
        resetWifi();
    }

    Bme280Sensor bme;
    float batteryVoltage = readBatteryVoltage(MONITOR_BATTERY_PIN);
    bme.measure();
    Data latestMeasurement{bme.temperature, bme.humidity, bme.pressure, batteryVoltage};
    logLatestMeasurement(latestMeasurement, usbConnected);

    DeviceConfig cfg;
    bool skipUpdateChecks = false;
    if (usbConnected) {
        cfg.measureIntervalSeconds = USB_MEASURE_INTERVAL_SECONDS;
        cfg.publishIntervalSeconds = USB_PUBLISH_INTERVAL_SECONDS;
        cfg.updateIntervalSeconds = USB_PUBLISH_INTERVAL_SECONDS;
        skipUpdateChecks = true;
    }
    cfg.normalizeIntervals();

    TimingConfig timing = computeTimingConfig(cfg);
    publishCount = constrain(publishCount, 0, timing.samplesPerPublish);
    if (timing.publishClamped) {
        long effectiveWindow = static_cast<long>(timing.samplesPerPublish) * timing.measureInterval;
        Log.warning("Publish interval too large for RTC buffer, using %ld seconds (%d samples)" CR, effectiveWindow,
                    timing.samplesPerPublish);
    }

    if (publishCount >= timing.samplesPerPublish) {
        publishCount = timing.samplesPerPublish - 1;
    }
    data[publishCount] = latestMeasurement;
    publishCount++;

    if (!skipUpdateChecks &&
        (updateCount == 0 || static_cast<long>(updateCount) * timing.measureInterval >= timing.updateInterval)) {
        connectToRouter(cfg);
        checkForUpdates(FW_IMAGE_URL, VERSION_URL, cfg);
        updateCount = 0;
    }
    updateCount++;

    if (publishCount >= timing.samplesPerPublish) {
        const int samplesToPublish = publishCount;
        long publishWindowSeconds = static_cast<long>(samplesToPublish) * timing.measureInterval;
        if (publishWindowSeconds <= 0) {
            publishWindowSeconds = timing.measureInterval;
        }
        connectToRouter(cfg);
        publishValues(data, samplesToPublish, static_cast<int>(publishWindowSeconds), cfg);
        publishCount = 0;
    }

    handleResetRequest();

    esp_sleep_enable_ext0_wakeup(MONITOR_RESET_PIN, HIGH);
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timing.measureInterval) * uS_TO_S_FACTOR);

    Log.notice("Going to sleep for %d seconds" CR, timing.measureInterval);

    esp_wifi_stop();
    esp_deep_sleep_start();
}

void loop() {}
