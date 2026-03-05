#include "battery.h"
#include "BoardConfig.h"

#include <Arduino.h>
#include <esp_log.h>
#include <math.h>

namespace {
constexpr char TAG[] = "battery";
}

float readBatteryVoltage(int pin) {
    const int sampleCount = MONITOR_BATTERY_ADC_SAMPLES > 0 ? MONITOR_BATTERY_ADC_SAMPLES : 1;
    const int adcChannel = digitalPinToAnalogChannel(pin);
    if (adcChannel < 0) {
        ESP_LOGE(TAG, "Battery pin %d does not map to an ADC channel", pin);
        return NAN;
    }

    pinMode(pin, INPUT);
    analogSetPinAttenuation(pin, static_cast<adc_attenuation_t>(MONITOR_BATTERY_ATTENUATION));

    uint32_t sumMilliVolts = 0;
    for (int i = 0; i < sampleCount; i++) {
        sumMilliVolts += analogReadMilliVolts(pin);
        if (MONITOR_BATTERY_ADC_SAMPLE_DELAY_MS > 0) {
            delay(MONITOR_BATTERY_ADC_SAMPLE_DELAY_MS);
        }
    }

    const float pinVoltageMv = static_cast<float>(sumMilliVolts) / static_cast<float>(sampleCount);
    const float batteryVoltageMv = pinVoltageMv * MONITOR_BATTERY_DIVIDER_RATIO;
    return batteryVoltageMv / 1000.0f;
}
