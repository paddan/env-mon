#include "logging_utils.h"

#include <ArduinoLog.h>
#include <esp_system.h>
#include <string.h>
#include <time.h>

namespace {
bool usbConnected = false;
constexpr uint32_t USB_DETECT_WINDOW_MS = 1500;
constexpr uint32_t USB_DETECT_POLL_MS = 20;
constexpr uint32_t USB_PLUG_SETTLE_MS = 120;

bool detectUsbNow() {
#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 1)
    return HWCDC::isPlugged();
#elif defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT == 1)
    return Serial;
#else
    // No reliable "host attached" signal on plain UART serial targets.
    return false;
#endif
}

bool detectUsb() {
    // HWCDC initializes as "plugged" very early; allow SOF state to settle first.
#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 1)
    delay(USB_PLUG_SETTLE_MS);
#endif

    if (detectUsbNow()) {
        return true;
    }

#if (defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT == 1)) ||                                  \
    (defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 1))
    // Wait briefly after cold boot so host monitor can assert DTR/RTS.
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        uint32_t start = millis();
        while ((millis() - start) < USB_DETECT_WINDOW_MS) {
            if (detectUsbNow()) {
                return true;
            }
            delay(USB_DETECT_POLL_MS);
        }
    }
#endif
    return false;
}
} // namespace

bool initLogging() {
    usbConnected = detectUsb();
    int logLevel = usbConnected ? LOG_LEVEL_VERBOSE : LOG_LEVEL_SILENT;
    Log.begin(logLevel, &Serial);
    if (usbConnected) {
        Log.notice("USB connection detected; forcing 10s measure / 10s publish intervals" CR);
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

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "[%s | %ld] Measurement: %.2f°C, %.1f%%, %.2f hPa, %.3f V", timestamp,
             static_cast<long>(now), measurement.temperature, measurement.humidity, measurement.pressure,
             measurement.batteryVoltage);
    Log.notice("%s" CR, buffer);
}
