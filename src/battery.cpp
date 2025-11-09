#include "battery.h"
#include "BoardConfig.h"

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#define NUM_MEASUREMENTS 64
#define DELAY_IN_MS 1
#define DEFAULT_VREF 1100

float map(int x, int in_min, int in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float readBatteryVoltage(int pin) {
    adc1_channel_t channel = static_cast<adc1_channel_t>(digitalPinToAnalogChannel(pin));
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel, MONITOR_BATTERY_ATTENUATION);

    // Characterize ADC at particular atten
    esp_adc_cal_characteristics_t *adc_chars =
        (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, MONITOR_BATTERY_ATTENUATION, ADC_WIDTH_BIT_12, DEFAULT_VREF,
                             adc_chars);

    double sum_raw = 0;
    for (int i = 0; i < NUM_MEASUREMENTS; i++) {
        sum_raw += adc1_get_raw(channel);
        delay(DELAY_IN_MS);
    }

    int raw = sum_raw / NUM_MEASUREMENTS;

    uint32_t voltage = 2 * esp_adc_cal_raw_to_voltage(raw, adc_chars);

    free(adc_chars);

    return (float)voltage / 1000;
}
