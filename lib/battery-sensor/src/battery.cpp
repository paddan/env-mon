#include "battery.h"
#include "BoardConfig.h"

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <math.h>

#define DEFAULT_VREF 1100

float readBatteryVoltage(int pin) {
    const int sampleCount = MONITOR_BATTERY_ADC_SAMPLES > 0 ? MONITOR_BATTERY_ADC_SAMPLES : 1;

    adc1_channel_t channel = static_cast<adc1_channel_t>(digitalPinToAnalogChannel(pin));
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel, MONITOR_BATTERY_ATTENUATION);

    // Characterize ADC at particular atten
    esp_adc_cal_characteristics_t *adc_chars =
        (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    if (!adc_chars) {
        return NAN;
    }
    esp_adc_cal_characterize(ADC_UNIT_1, MONITOR_BATTERY_ATTENUATION, ADC_WIDTH_BIT_12, DEFAULT_VREF,
                             adc_chars);

    double sum_raw = 0;
    for (int i = 0; i < sampleCount; i++) {
        sum_raw += adc1_get_raw(channel);
        if (MONITOR_BATTERY_ADC_SAMPLE_DELAY_MS > 0) {
            delay(MONITOR_BATTERY_ADC_SAMPLE_DELAY_MS);
        }
    }

    int raw = sum_raw / sampleCount;

    uint32_t voltage = 2 * esp_adc_cal_raw_to_voltage(raw, adc_chars);

    free(adc_chars);

    return (float)voltage / 1000;
}
