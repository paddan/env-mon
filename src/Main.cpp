#include "BoardConfig.h"
#include <Arduino.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <zigbee_env_monitor.h>

namespace {
constexpr char TAG[] = "envmon-main";
constexpr uint32_t FACTORY_RESET_HOLD_MS = 3000UL;
constexpr uint32_t JOIN_STATUS_LOG_INTERVAL_MS = 5000UL;
constexpr uint32_t ZIGBEE_START_TIMEOUT_MS = 120000UL;

#if MONITOR_MEASURE_INTERVAL_S < 1
#error "MONITOR_MEASURE_INTERVAL_S must be >= 1"
#endif
constexpr uint32_t MEASURE_INTERVAL_S = MONITOR_MEASURE_INTERVAL_S;

constexpr ZigbeeReportingConfig ZIGBEE_REPORTING_CONFIG = {
    .sensorEndpoint = 10,
    .pressureEndpoint = 11,
    .reportMinIntervalS = 10,
    .reportMaxIntervalS = 300,
    .reportTempDeltaC = 0.2f,
    .reportHumidityDeltaPct = 1.0f,
    .reportPressureDeltaHpa = 1,
    .manufacturer = "JPL Design",
    .model = "EnvMon-XXXXX",
};

constexpr ZigbeeJoinConfig ZIGBEE_JOIN_CONFIG = {
    .zigbeeStartTimeoutMs = ZIGBEE_START_TIMEOUT_MS,
    .joinStatusLogIntervalMs = JOIN_STATUS_LOG_INTERVAL_MS,
};

ZigbeeEnvMonitor zigbeeMonitor(ZIGBEE_REPORTING_CONFIG, ZIGBEE_JOIN_CONFIG);

bool resetPinArmed = false;
uint32_t resetPressStartedMs = 0;

void flushLogs() {
    if (static_cast<bool>(Serial)) {
        Serial.flush();
    }
}

bool isResetPressed() {
#if MONITOR_RESET_ACTIVE_HIGH
    return digitalRead(MONITOR_RESET_PIN) == HIGH;
#else
    return digitalRead(MONITOR_RESET_PIN) == LOW;
#endif
}

bool resetButtonHeldForFactoryReset() {
    if (!resetPinArmed) {
        if (!isResetPressed()) {
            resetPinArmed = true;
        }
        return false;
    }

    if (!isResetPressed()) {
        resetPressStartedMs = 0;
        return false;
    }

    if (resetPressStartedMs == 0) {
        resetPressStartedMs = millis();
        return false;
    }

    return (millis() - resetPressStartedMs) >= FACTORY_RESET_HOLD_MS;
}

void enterDeepSleep() {
    const uint64_t sleepDurationUs = static_cast<uint64_t>(MEASURE_INTERVAL_S) * 1000000ULL;
    ESP_LOGI(TAG, "Entering deep sleep for %u seconds", MEASURE_INTERVAL_S);
    flushLogs();
    const esp_err_t wakeupErr = esp_sleep_enable_timer_wakeup(sleepDurationUs);
    if (wakeupErr != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure timer wakeup (%d), restarting", static_cast<int>(wakeupErr));
        flushLogs();
        ESP.restart();
    }

    esp_deep_sleep_start();

    ESP_LOGE(TAG, "esp_deep_sleep_start returned unexpectedly, restarting");
    flushLogs();
    ESP.restart();
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.setDebugOutput(true);

#if MONITOR_RESET_ACTIVE_HIGH
    pinMode(MONITOR_RESET_PIN, INPUT_PULLDOWN);
#else
    pinMode(MONITOR_RESET_PIN, INPUT_PULLUP);
#endif

    bool zigbeeReady = false;
    zigbeeMonitor.addAndConfigureEndpoints();
    if (!zigbeeMonitor.beginEndDevice()) {
        ESP_LOGW(TAG, "Zigbee failed to start; continuing with local measurement logging only");
    } else {
        zigbeeMonitor.publishMeasurement();
        zigbeeReady = zigbeeMonitor.waitForNetworkJoin(resetButtonHeldForFactoryReset);
        if (!zigbeeReady) {
            ESP_LOGW(TAG, "Zigbee is not connected yet; local logs will continue until a network is available");
        }
        enterDeepSleep();
        return;
    }

    zigbeeMonitor.publishMeasurement();
    enterDeepSleep();
}

void loop() {
    delay(1000);
}
