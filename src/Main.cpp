#include "BoardConfig.h"
#include "driver/adc.h"
#include "logging_utils.h"
#include "mqtt.h"
#include <Arduino.h>
#include <ArduinoLog.h>
#include <DeviceConfig.h>
#include <PubSubClient.h>
#include <battery.h>
#include <bme280.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <runtime_policy.h>
#include <wifi_handler.h>
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

#if defined(CONFIG_IDF_TARGET_ESP32)
#include <esp_bt.h>
#include <esp_bt_main.h>
#endif

#define uS_TO_S_FACTOR 1000000LLU /* Conversion factor for micro seconds to seconds */
#define RESET_HOLD_MS 2000        /* Hold reset button for 2s to wipe config */
#define RESET_POLL_INTERVAL_MS 50
constexpr int USB_MEASURE_INTERVAL_SECONDS = 10;
constexpr int USB_PUBLISH_INTERVAL_SECONDS = 60;

constexpr int MAX_MEASUREMENTS = 120;

RTC_DATA_ATTR int publishCount = 0;
RTC_DATA_ATTR Data data[MAX_MEASUREMENTS];

namespace {
bool resetPinArmed = false;
bool usbContinuousMode = false;
bool runtimeReady = false;
uint32_t nextUsbMeasureMs = 0;
DeviceConfig *runtimeCfg = nullptr;

runtime_policy::TimingConfig runtimeTiming{};

Data readLatestMeasurement() {
    Bme280Sensor bme;
    float batteryVoltage = readBatteryVoltage(MONITOR_BATTERY_PIN);
    bme.measure();

    Data latestMeasurement{};
    latestMeasurement.temperature = bme.temperature;
    latestMeasurement.humidity = bme.humidity;
    latestMeasurement.pressure = bme.pressure;
    latestMeasurement.batteryVoltage = batteryVoltage;
    return latestMeasurement;
}

bool publishBufferedSamples(DeviceConfig &cfg, const runtime_policy::TimingConfig &timing) {
    if (publishCount <= 0) {
        return true;
    }

    const int samplesToPublish = publishCount;
    long publishWindowSeconds = runtime_policy::computePublishWindowSeconds(samplesToPublish, timing.measureInterval);

    connectToRouter(cfg);
    if (publishValues(data, samplesToPublish, static_cast<int>(publishWindowSeconds), cfg)) {
        publishCount = 0;
        return true;
    }

    Log.warning("Publish failed; preserving %d buffered samples for retry" CR, samplesToPublish);
    return false;
}

void collectAndMaybePublish(DeviceConfig &cfg, const runtime_policy::TimingConfig &timing) {
    publishCount = runtime_policy::clampPublishCount(publishCount, timing.samplesPerPublish);

    // If buffer is already full (previous publish failed), retry before taking new samples.
    if (!runtime_policy::canCollectSample(publishCount, timing.samplesPerPublish)) {
        if (!publishBufferedSamples(cfg, timing)) {
            return;
        }
    }

    Data latestMeasurement = readLatestMeasurement();
    logLatestMeasurement(latestMeasurement);

    data[publishCount] = latestMeasurement;
    publishCount++;

    if (!runtime_policy::canCollectSample(publishCount, timing.samplesPerPublish)) {
        publishBufferedSamples(cfg, timing);
    }
}

bool isResetPressed() {
#if MONITOR_RESET_ACTIVE_HIGH
    return digitalRead(MONITOR_RESET_PIN) == HIGH;
#else
    return digitalRead(MONITOR_RESET_PIN) == LOW;
#endif
}

bool resetButtonHeld(uint32_t holdMs = RESET_HOLD_MS) {
    // Require seeing an idle state first to avoid false "held" triggers if the line is stuck active at boot.
    if (!resetPinArmed) {
        if (!isResetPressed()) {
            resetPinArmed = true;
        }
        return false;
    }

    if (!isResetPressed()) {
        return false;
    }

    uint32_t elapsed = 0;
    while (elapsed < holdMs) {
        if (!isResetPressed()) {
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

void logStoredConfigAtBoot(const DeviceConfig &cfg) {
    Log.notice(
        "Stored config: device='%s', mqtt='%s:%d', topic='%s', user='%s', password_set=%s, measure=%ds, publish=%ds, "
        "fw=%d" CR,
        cfg.deviceName.c_str(), cfg.mqttHost.c_str(), cfg.mqttPort, cfg.mqttTopic.c_str(), cfg.mqttUser.c_str(),
        cfg.mqttPassword.isEmpty() ? "no" : "yes", cfg.measureIntervalSeconds, cfg.publishIntervalSeconds, cfg.fwVersion);
}

#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 1)
void waitForUsbConsoleAttach() {
    if (!HWCDC::isPlugged()) {
        return;
    }

    const uint32_t start = millis();
    const uint32_t timeoutMs = 4000;
    while (!Serial && (millis() - start) < timeoutMs) {
        delay(20);
    }
}

#endif

} // namespace

void setup() {
    Serial.begin(115200);
#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 1)
    waitForUsbConsoleAttach();
#endif
#if MONITOR_RESET_ACTIVE_HIGH
    pinMode(MONITOR_RESET_PIN, INPUT_PULLDOWN);
#else
    pinMode(MONITOR_RESET_PIN, INPUT_PULLUP);
#endif

    bool usbConnected = initLogging();

#if defined(CONFIG_IDF_TARGET_ESP32)
    esp_bluedroid_disable();
    esp_bt_controller_disable();
#endif

    handleResetRequest();

#if defined(CONFIG_IDF_TARGET_ESP32)
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        Log.warning("Resetting wifi!" CR);
        resetWifi();
    }
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) ||                                      \
    defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32H2)
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        uint64_t wakeMask = esp_sleep_get_gpio_wakeup_status();
        if (wakeMask & (1ULL << MONITOR_RESET_PIN)) {
            Log.warning("Resetting wifi!" CR);
            resetWifi();
        }
    }
#endif

    static DeviceConfig cfg;
    if (!cfg.loadedFromStorage) {
        Log.warning("No stored config in SPIFFS, starting WiFiManager provisioning" CR);
        connectToRouter(cfg, true);
    }

    if (usbConnected) {
        cfg.measureIntervalSeconds = USB_MEASURE_INTERVAL_SECONDS;
        cfg.publishIntervalSeconds = USB_PUBLISH_INTERVAL_SECONDS;
    }
    cfg.normalizeIntervals();
    logStoredConfigAtBoot(cfg);

    runtime_policy::TimingConfig timing =
        runtime_policy::computeTimingConfig(cfg.measureIntervalSeconds, cfg.publishIntervalSeconds, MAX_MEASUREMENTS);
    if (timing.publishClamped) {
        long effectiveWindow = static_cast<long>(timing.samplesPerPublish) * timing.measureInterval;
        Log.warning("Publish interval too large for RTC buffer, using %ld seconds (%d samples)" CR, effectiveWindow,
                    timing.samplesPerPublish);
    }
    runtimeCfg = &cfg;
    runtimeTiming = timing;
    runtimeReady = true;
    usbContinuousMode = usbConnected;

    collectAndMaybePublish(cfg, timing);

    handleResetRequest();

    // Keep USB development sessions stable: deep sleep disconnects USB and causes monitor reconnect/reset churn.
    if (usbConnected) {
        nextUsbMeasureMs = millis() + (static_cast<uint32_t>(timing.measureInterval) * 1000UL);
        Log.notice("USB connected; continuous mode enabled (no deep sleep)" CR);
        return;
    }

#if defined(CONFIG_IDF_TARGET_ESP32)
#if MONITOR_RESET_ACTIVE_HIGH
    esp_sleep_enable_ext0_wakeup(MONITOR_RESET_PIN, HIGH);
#else
    esp_sleep_enable_ext0_wakeup(MONITOR_RESET_PIN, LOW);
#endif
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) ||                                      \
    defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32H2)
    // ESP32-C3/C2/C6/H2 use GPIO wake instead of EXT0.
#if MONITOR_RESET_ACTIVE_HIGH
    esp_deep_sleep_enable_gpio_wakeup(1ULL << MONITOR_RESET_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
#else
    esp_deep_sleep_enable_gpio_wakeup(1ULL << MONITOR_RESET_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
#endif
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timing.measureInterval) * uS_TO_S_FACTOR);

    Log.notice("Going to sleep for %d seconds" CR, timing.measureInterval);

    esp_wifi_stop();
    esp_deep_sleep_start();
}

void loop() {
    handleResetRequest();
    if (runtimeReady && usbContinuousMode && runtimeCfg != nullptr) {
        const uint32_t now = millis();
        if (static_cast<int32_t>(now - nextUsbMeasureMs) >= 0) {
            collectAndMaybePublish(*runtimeCfg, runtimeTiming);
            nextUsbMeasureMs = now + (static_cast<uint32_t>(runtimeTiming.measureInterval) * 1000UL);
        }
    }
    delay(RESET_POLL_INTERVAL_MS);
}
