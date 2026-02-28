#pragma once

#include <driver/adc.h>
#include <driver/gpio.h>

// Allow overriding through build flags while keeping workable defaults for ESP32-C3 SuperMini.
#ifndef MONITOR_RESET_PIN
#define MONITOR_RESET_PIN GPIO_NUM_2
#endif

// Reset input polarity: 1 = active-high (pressed when pin is HIGH), 0 = active-low.
#ifndef MONITOR_RESET_ACTIVE_HIGH
#define MONITOR_RESET_ACTIVE_HIGH 0
#endif

#ifndef MONITOR_BATTERY_PIN
#define MONITOR_BATTERY_PIN GPIO_NUM_4
#endif

#ifndef MONITOR_BATTERY_ATTENUATION
#define MONITOR_BATTERY_ATTENUATION ADC_ATTEN_DB_12
#endif

#ifndef MONITOR_BATTERY_ADC_SAMPLES
#define MONITOR_BATTERY_ADC_SAMPLES 16
#endif

#ifndef MONITOR_BATTERY_ADC_SAMPLE_DELAY_MS
#define MONITOR_BATTERY_ADC_SAMPLE_DELAY_MS 0
#endif

#ifndef MONITOR_BME280_SDA_PIN
#error "MONITOR_BME280_SDA_PIN must be provided via platformio.ini build_flags"
#endif

#ifndef MONITOR_BME280_SCL_PIN
#error "MONITOR_BME280_SCL_PIN must be provided via platformio.ini build_flags"
#endif

// NTP and local timezone configuration for timestamping.
#ifndef MONITOR_NTP_SERVER
#define MONITOR_NTP_SERVER "pool.ntp.org"
#endif

#ifndef MONITOR_TIMEZONE
#define MONITOR_TIMEZONE "UTC0"
#endif

#ifndef MONITOR_NTP_SYNC_INTERVAL_SECONDS
#define MONITOR_NTP_SYNC_INTERVAL_SECONDS (24 * 60 * 60) // 24h
#endif

#ifndef MONITOR_WIFI_FAST_CONNECT_TIMEOUT_MS
#define MONITOR_WIFI_FAST_CONNECT_TIMEOUT_MS 4000
#endif
