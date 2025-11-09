#pragma once

#include <driver/adc.h>
#include <driver/gpio.h>

// Allow overriding through build flags while keeping workable defaults for FireBeetle32.
#ifndef MONITOR_RESET_PIN
#define MONITOR_RESET_PIN GPIO_NUM_14
#endif

#ifndef MONITOR_BATTERY_PIN
#define MONITOR_BATTERY_PIN GPIO_NUM_39
#endif

#ifndef MONITOR_I2C_SDA_PIN
#define MONITOR_I2C_SDA_PIN 21
#endif

#ifndef MONITOR_I2C_SCL_PIN
#define MONITOR_I2C_SCL_PIN 22
#endif

#ifndef MONITOR_BATTERY_ATTENUATION
#define MONITOR_BATTERY_ATTENUATION ADC_ATTEN_DB_11
#endif
