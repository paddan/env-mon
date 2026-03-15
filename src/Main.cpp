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
constexpr uint32_t FIRST_JOIN_AWAKE_S = MONITOR_FIRST_JOIN_AWAKE_S;

constexpr ZigbeeReportingConfig ZIGBEE_REPORTING_CONFIG = {
    .sensorEndpoint = 10,
    .pressureEndpoint = 11,
    .rssiEndpoint = 12,
    .txPowerEndpoint = 13,
    .reportMinIntervalS = 10,
    .reportMaxIntervalS = 300,
    .reportTempDeltaC = 0.2f,
    .reportHumidityDeltaPct = 1.0f,
    .reportPressureDeltaHpa = 1,
    .reportRssiDeltaDbm = 1.0f,
    .reportTxPowerDeltaDbm = 1.0f,
    .manufacturer = "JPL Design",
    .model = "EnvMon-XXXXX",
};

constexpr ZigbeeJoinConfig ZIGBEE_JOIN_CONFIG = {
    .zigbeeStartTimeoutMs = ZIGBEE_START_TIMEOUT_MS,
    .rejoinTimeoutMs = MONITOR_ZIGBEE_REJOIN_TIMEOUT_MS,
    .joinStatusLogIntervalMs = JOIN_STATUS_LOG_INTERVAL_MS,
};

ZigbeeEnvMonitor zigbeeMonitor(ZIGBEE_REPORTING_CONFIG, ZIGBEE_JOIN_CONFIG);

bool resetPinArmed = false;
uint32_t resetPressStartedMs = 0;
RTC_DATA_ATTR bool firstJoinInterviewCompleted = false;
RTC_DATA_ATTR uint32_t rtcStateVersion = 0;
bool activeModeEnabled = false;
uint32_t lastActiveMeasurementMs = 0;

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

bool isUsbConsoleConnected() {
    return static_cast<bool>(Serial);
}

bool shouldStayAwake() {
#if MONITOR_DISABLE_DEEP_SLEEP
    return true;
#else
    return isUsbConsoleConnected();
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

    if ((millis() - resetPressStartedMs) >= FACTORY_RESET_HOLD_MS) {
        firstJoinInterviewCompleted = false;
        return true;
    }

    return false;
}

void restartZigbeeJoin(const char *reason) {
    firstJoinInterviewCompleted = false;
    ESP_LOGW(TAG, "%s", reason);
    if (zigbeeMonitor.isStarted()) {
        zigbeeMonitor.factoryResetNetwork();
    }
    flushLogs();
    delay(100);
    ESP.restart();
}

void keepAwakeForInitialInterview() {
    if (FIRST_JOIN_AWAKE_S == 0 || firstJoinInterviewCompleted) {
        return;
    }

    const uint32_t awakeMs = FIRST_JOIN_AWAKE_S * 1000UL;
    const uint32_t awakeStartMs = millis();
    uint32_t lastLogElapsedMs = 0;

    ESP_LOGI(TAG, "Keeping device awake for %u seconds so Home Assistant can finish Zigbee interview",
             FIRST_JOIN_AWAKE_S);

    while ((millis() - awakeStartMs) < awakeMs) {
        if (resetButtonHeldForFactoryReset()) {
            restartZigbeeJoin("Reset button held during initial interview window; restarting Zigbee join");
        }

        const uint32_t elapsedMs = millis() - awakeStartMs;
        if ((elapsedMs - lastLogElapsedMs) >= 10000UL) {
            const uint32_t remainingMs = awakeMs - elapsedMs;
            ESP_LOGI(TAG, "Initial interview window still open for %lu seconds",
                     static_cast<unsigned long>((remainingMs + 999UL) / 1000UL));
            lastLogElapsedMs = elapsedMs;
        }
        delay(250);
    }

    firstJoinInterviewCompleted = true;
}

void enterDeepSleep() {
    if (shouldStayAwake()) {
        if (isUsbConsoleConnected()) {
            ESP_LOGI(TAG, "USB serial is connected; skipping deep sleep and continuing periodic measurements");
        } else {
            ESP_LOGI(TAG, "Deep sleep disabled; continuing periodic measurements");
        }
        flushLogs();
        activeModeEnabled = true;
        lastActiveMeasurementMs = millis();
        return;
    }

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

    // Invalidate RTC-persisted state when MONITOR_FIRMWARE_VERSION is bumped after a firmware update.
    // RTC RAM survives deep sleep but is not cleared by OTA flashing, so learned TX power and the
    // interview-completed flag can carry stale values across incompatible firmware changes.
    if (rtcStateVersion != static_cast<uint32_t>(MONITOR_FIRMWARE_VERSION)) {
        ESP_LOGI(TAG, "Firmware version changed (rtcVer=%lu → fwVer=%lu); resetting persistent RTC state",
                 static_cast<unsigned long>(rtcStateVersion),
                 static_cast<unsigned long>(MONITOR_FIRMWARE_VERSION));
        firstJoinInterviewCompleted = false;
        ZigbeeEnvMonitor::resetAdaptiveTxPowerRtcState();
        rtcStateVersion = static_cast<uint32_t>(MONITOR_FIRMWARE_VERSION);
    }

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
        } else if (!shouldStayAwake()) {
            keepAwakeForInitialInterview();
        }
        enterDeepSleep();
        return;
    }

    zigbeeMonitor.publishMeasurement();
    enterDeepSleep();
}

void loop() {
    if (activeModeEnabled) {
        if (resetButtonHeldForFactoryReset()) {
            restartZigbeeJoin("Reset button held while running; restarting Zigbee join");
        }

        const uint32_t now = millis();
        if ((now - lastActiveMeasurementMs) >= (MEASURE_INTERVAL_S * 1000UL)) {
            zigbeeMonitor.publishMeasurement();
            lastActiveMeasurementMs = now;
        }

        delay(100);
        return;
    }

    delay(1000);
}
