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

#ifndef MONITOR_BATTERY_ATTENUATION
#define MONITOR_BATTERY_ATTENUATION ADC_ATTEN_DB_12
#endif

#ifndef MONITOR_BME280_SDA_PIN
#error "MONITOR_BME280_SDA_PIN must be provided via platformio.ini build_flags"
#endif

#ifndef MONITOR_BME280_SCL_PIN
#error "MONITOR_BME280_SCL_PIN must be provided via platformio.ini build_flags"
#endif

#ifndef MONITOR_PMS5003_ENABLED
#error "MONITOR_PMS5003_ENABLED must be provided via platformio.ini build_flags (0 to disable)"
#endif

#if MONITOR_PMS5003_ENABLED
#ifndef MONITOR_PMS5003_UART_NUM
#error "MONITOR_PMS5003_UART_NUM must be provided via platformio.ini build_flags when PMS5003 is enabled"
#endif

#ifndef MONITOR_PMS5003_RX_PIN
#error "MONITOR_PMS5003_RX_PIN must be provided via platformio.ini build_flags when PMS5003 is enabled"
#endif

#ifndef MONITOR_PMS5003_TX_PIN
#error "MONITOR_PMS5003_TX_PIN must be provided via platformio.ini build_flags when PMS5003 is enabled"
#endif

#ifndef MONITOR_PMS5003_BAUD
#error "MONITOR_PMS5003_BAUD must be provided via platformio.ini build_flags when PMS5003 is enabled"
#endif
#endif
