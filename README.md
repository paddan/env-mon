# env-mon

ESP32-C6 Zigbee end-device firmware for EnvMon using a BME280 sensor.

## Project structure

- `src/` main firmware entrypoint (`Main.cpp`)
- `include/` shared project config headers (`BoardConfig.h`)
- `lib/bme280-sensor/` BME280 initialization + measurement helper
- `lib/battery-sensor/` battery voltage sampling helper
- `lib/zigbee-env-monitor/` Zigbee endpoints, join flow, and reporting orchestration
- `zigbee.csv` partition layout tuned for Zigbee storage + coredump

`idf_component.yml` is kept for ESP-IDF compatibility metadata; builds in this repo are done with PlatformIO (`platformio.ini`).

## Build

```sh
pio run -e esp32c6_zigbee_ed
```

## Upload

```sh
pio run -e esp32c6_zigbee_ed -t upload
```

## Monitor

```sh
pio device monitor -b 115200
```

## Features

- Zigbee temperature + humidity endpoint
- Zigbee pressure endpoint
- Zigbee Diagnostics attributes for link metrics (`LastRSSI`, `LastLQI`)
- Zigbee custom cluster (`0xFC00`) with TX power attribute (`0x0000`, dBm)
- Unique Zigbee model identity per device (`EnvMon-XXXXX`, derived from chip ID)
- Local serial measurement logging even when Zigbee is not connected
- Configurable BME280 reporting interval (`MONITOR_MEASURE_INTERVAL_S`)
- Deep sleep between measurements
- Factory reset of Zigbee NVRAM by holding reset button for ~3 seconds

## Pin defaults

- `MONITOR_BME280_SDA_PIN=D4`
- `MONITOR_BME280_SCL_PIN=D5`
- `MONITOR_BATTERY_PIN=A0` (ADC-capable on XIAO ESP32-C6)
- `MONITOR_BATTERY_DIVIDER_RATIO=2.0f`
- `MONITOR_RESET_PIN=GPIO_NUM_2` (active-low)
- `MONITOR_MEASURE_INTERVAL_S=30`

Adjust pins in `platformio.ini` build flags as needed.

## XIAO ESP32-C6 battery note

For battery voltage sensing on XIAO ESP32-C6, route VBAT to your configured ADC pin through a divider (typically 200k/200k, ratio 2:1).  
If this path is missing, battery reads will stay near 0 V.

## Without Zigbee network

If no Zigbee coordinator/network is available yet, firmware will still boot, take a measurement, and print it to serial logs.
It will retry Zigbee join on subsequent wakeups while continuing local measurement logging.

## Link metrics reporting

Each measurement cycle logs and (when connected) publishes:

- `RSSI` and `LQI` from the most recent received IEEE 802.15.4 frame
- Zigbee transmit power (`TX power`, dBm) read from the Zigbee stack

Zigbee mapping on endpoint `10`:

- Diagnostics cluster (`0x0B05`)
  - `LastLQI` (`0x011C`)
  - `LastRSSI` (`0x011D`)
- Custom cluster (`0xFC00`)
  - `TX power dBm` (`0x0000`, signed 8-bit)
