#include "logging_utils.h"

#include <ArduinoLog.h>
#include <string.h>
#include <time.h>

namespace {
bool usbConnected = false;

bool detectUsb() {
#if defined(ARDUINO_USB_CDC_ON_BOOT) || defined(ARDUINO_USB_MODE)
    return Serial;
#else
    return Serial.dtr() || Serial.rts();
#endif
}
} // namespace

bool initLogging() {
    usbConnected = detectUsb();
    int logLevel = usbConnected ? LOG_LEVEL_VERBOSE : LOG_LEVEL_SILENT;
    Log.begin(logLevel, &Serial);
    if (usbConnected) {
        Log.notice("USB connection detected; forcing 10s measure / 60s publish intervals and disabling OTA updates" CR);
    }
    return usbConnected;
}

bool isLoggingEnabled() { return usbConnected; }

void logLatestMeasurement(const Data &measurement) {
    if (!usbConnected) {
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

    char buffer[192];
    snprintf(buffer, sizeof(buffer), "[%s | %ld] Measurement: %.2f°C, %.1f%%, %.2f hPa, %.3f V", timestamp,
             static_cast<long>(now), measurement.temperature, measurement.humidity, measurement.pressure,
             measurement.batteryVoltage);
    Log.notice("%s" CR, buffer);
}
