#pragma once

#include <driver/gpio.h>

// Allow overriding through build flags while keeping workable defaults for ESP32-C6 DevKitM-1.
#ifndef MONITOR_RESET_PIN
#define MONITOR_RESET_PIN GPIO_NUM_2
#endif

// Reset input polarity: 1 = active-high (pressed when pin is HIGH), 0 = active-low.
#ifndef MONITOR_RESET_ACTIVE_HIGH
#define MONITOR_RESET_ACTIVE_HIGH 0
#endif

#ifndef MONITOR_BATTERY_PIN
#ifdef ARDUINO_XIAO_ESP32C6
#define MONITOR_BATTERY_PIN GPIO_NUM_0
#else
#define MONITOR_BATTERY_PIN GPIO_NUM_4
#endif
#endif

#ifndef MONITOR_BATTERY_ATTENUATION
#define MONITOR_BATTERY_ATTENUATION ADC_11db
#endif

#ifndef MONITOR_BATTERY_ADC_SAMPLES
#define MONITOR_BATTERY_ADC_SAMPLES 16
#endif

#ifndef MONITOR_BATTERY_ADC_SAMPLE_DELAY_MS
#define MONITOR_BATTERY_ADC_SAMPLE_DELAY_MS 0
#endif

#ifndef MONITOR_BATTERY_DIVIDER_RATIO
#define MONITOR_BATTERY_DIVIDER_RATIO 2.0f
#endif

#ifndef MONITOR_BME280_SDA_PIN
#error "MONITOR_BME280_SDA_PIN must be provided via platformio.ini build_flags"
#endif

#ifndef MONITOR_BME280_SCL_PIN
#error "MONITOR_BME280_SCL_PIN must be provided via platformio.ini build_flags"
#endif

#ifndef MONITOR_MEASURE_INTERVAL_S
#define MONITOR_MEASURE_INTERVAL_S 30
#endif

#ifndef MONITOR_ZIGBEE_EXPLICIT_REPORTS
#define MONITOR_ZIGBEE_EXPLICIT_REPORTS 0
#endif

#ifndef MONITOR_FIRST_JOIN_AWAKE_S
#define MONITOR_FIRST_JOIN_AWAKE_S 180
#endif

#ifndef MONITOR_DISABLE_DEEP_SLEEP
#define MONITOR_DISABLE_DEEP_SLEEP 0
#endif
