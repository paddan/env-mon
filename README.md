# env-mon

ESP32-C6 Zigbee end-device firmware for EnvMon using a BME280 sensor.

## Project structure

- `src/` main firmware entrypoint (`Main.cpp`)
- `include/` shared project config headers (`BoardConfig.h`)
- `lib/bme280-sensor/` BME280 initialization + measurement helper
- `lib/battery-sensor/` battery voltage sampling helper
- `lib/zigbee-env-monitor/` Zigbee endpoints, join flow, and reporting orchestration
- `zigbee.csv` partition layout tuned for Zigbee storage + coredump

Builds in this repo are done with PlatformIO (`platformio.ini`).

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
- Zigbee RSSI endpoint
- Zigbee TX power endpoint
- Zigbee Diagnostics attributes for link metrics (`LastRSSI`, `LastLQI`)
- Zigbee custom cluster (`0xFC00`) with TX power attribute (`0x0000`, dBm)
- Unique Zigbee model identity per device (`EnvMon-XXXXX`, derived from chip ID)
- Local serial measurement logging even when Zigbee is not connected
- Configurable BME280 reporting interval (`MONITOR_MEASURE_INTERVAL_S`)
- Deep sleep between measurements
- Short battery-friendly rejoin timeout after the device has already been paired once
- Factory reset of Zigbee NVRAM by holding reset button for ~3 seconds

## Pin defaults

- `MONITOR_BME280_SDA_PIN=D4`
- `MONITOR_BME280_SCL_PIN=D5`
- `MONITOR_BATTERY_PIN=A0` (ADC-capable on XIAO ESP32-C6)
- `MONITOR_BATTERY_DIVIDER_RATIO=2.0f`
- `MONITOR_RESET_PIN=GPIO_NUM_2` (active-low)
- `MONITOR_MEASURE_INTERVAL_S=30`
- `MONITOR_ZIGBEE_REJOIN_TIMEOUT_MS=15000`

Adjust pins in `platformio.ini` build flags as needed.

## XIAO ESP32-C6 battery note

For battery voltage sensing on XIAO ESP32-C6, route VBAT to your configured ADC pin through a divider (typically 200k/200k, ratio 2:1).  
If this path is missing, battery reads will stay near 0 V.

## Without Zigbee network

If no Zigbee coordinator/network is available yet, firmware will still boot, take a measurement, and print it to serial logs.
It will retry Zigbee join on subsequent wakeups while continuing local measurement logging.

## Zigbee behavior

This firmware acts as a Zigbee end device. It measures locally, updates Zigbee attributes, and then either stays awake or sleeps depending on how it is powered and connected.

### Startup and join flow

1. The device boots and reads BME280 + battery values.
2. Zigbee starts in end-device mode.
3. If the node is not already joined, it starts network steering and waits for a coordinator.
4. After a successful join, ZHA interviews the device and discovers its endpoints.
5. During battery-powered operation, the node normally sleeps between measurements.

When USB serial is connected, deep sleep is skipped so the device stays awake for development, pairing, and debugging.

### Endpoints exposed to Home Assistant

- endpoint `10`: temperature, humidity, battery, diagnostics attributes
- endpoint `11`: pressure
- endpoint `12`: RSSI as an Analog Input sensor
- endpoint `13`: TX power as an Analog Input sensor

ZHA should normally create entities for temperature, humidity, pressure, battery, and may also expose RSSI / TX power after a fresh interview.
If endpoint `12` or `13` was added after the device was already paired, re-pair the node so ZHA interviews the new endpoint layout.
If the device is factory new and pairing to ZHA, it uses maximum Zigbee TX power. For an already joined device, normal boots reuse the learned TX power from previous reconnect attempts, TX power is increased when join or rejoin takes too long, and reduced one step after several successful joins in a row.

## Home Assistant

This device is intended to join a Home Assistant Zigbee network as a Zigbee end device.

### Pairing with ZHA

1. In Home Assistant, go to `Settings -> Devices & Services -> Zigbee Home Automation`.
2. Click `Add Device` and put ZHA into permit-join mode.
3. Power up the device, or reboot it after flashing.
4. Wait for the device to join.

It should appear with:

- manufacturer: `JPL Design`
- model: `EnvMon-XXXXX`

### Expected entities

With ZHA, the standard clusters should normally expose:

- temperature
- humidity
- pressure
- battery level
- RSSI
- TX power

Pressure may be shown with a different unit name depending on integration handling, but the firmware publishes pressure from the Zigbee pressure measurement cluster.
RSSI and TX power are exposed on separate Analog Input endpoints and may require a fresh join/interview before they appear.

### Custom diagnostics attributes

The firmware also exposes extra link metrics on endpoint `10`:

- Diagnostics cluster `0x0B05`
  - `LastLQI` `0x011C`
  - `LastRSSI` `0x011D`
- Custom cluster `0xFC00`
  - `TX power dBm` `0x0000`

These extra attributes are useful for diagnostics, but Home Assistant does not always create entities for them automatically.
In ZHA you can inspect them from the device page via cluster management / attribute reads if needed.

### Re-pairing / factory reset

If the device does not join as expected, clear its Zigbee state and pair again:

- hold the reset button for about 3 seconds to factory-reset Zigbee NVRAM
- then start permit-join in Home Assistant again and reboot the device

The reset input is active-low by default on `GPIO_NUM_2`, so a button press should pull the pin to `GND`.

### ZHA troubleshooting

If the device joins the Zigbee network but does not show up as a usable Home Assistant device, check these points:

- make sure you are not looking at the coordinator device page; the coordinator is typically `CC2652` / `Texas Instruments` and has `Nwk: 0x0000`
- the EnvMon device should appear separately with IEEE `58:e6:c5:ff:fe:19:ae:20`, manufacturer `JPL Design`, and model `EnvMon-XXXXX`
- if needed, enable temporary debug logging in Home Assistant using the `logger.set_level` action for:
  - `zha: debug`
  - `zigpy: debug`
  - `zigpy_znp: debug`
- during pairing, look for log lines similar to:
  - `New device ... joined the network`
  - `Requesting 'Node Descriptor'`
  - `Discovered endpoints`
  - `SimpleDescRsp`
- if pairing still fails, remove any stale EnvMon device entry in ZHA, start `Add device` again, then trigger a Zigbee factory reset on the node and retry

When USB serial is connected, the firmware stays awake and skips deep sleep to make ZHA interview/debugging easier.

### Sleep behavior

- USB connected: the node stays awake, keeps the serial monitor alive, and publishes periodically
- battery / standalone mode: the node measures, updates Zigbee, and then enters deep sleep for `MONITOR_MEASURE_INTERVAL_S`
- after the first successful join, the node can stay awake for `MONITOR_FIRST_JOIN_AWAKE_S` seconds to give ZHA time to finish its initial interview
- already-paired battery boots use `MONITOR_ZIGBEE_REJOIN_TIMEOUT_MS` before giving up and sleeping again, instead of burning the full first-pair timeout on every failed rejoin

## Battery power notes

- The BME280 runs in forced mode, so it returns to sleep after each measurement instead of staying in continuous-conversion mode.
- Full I2C bus scans are disabled during normal boots because they add unnecessary wake time and battery cost. If you need them for bring-up, set `MONITOR_BME280_SCAN_AT_BOOT=1`.
- The biggest battery cost is time spent awake with the Zigbee radio up. Keep `MONITOR_MEASURE_INTERVAL_S` as high as your use case allows, and keep the rejoin timeout short for already-paired devices.
- `CORE_DEBUG_LEVEL=3` is useful during bring-up but increases awake CPU time. For a production battery build, lowering log verbosity is worth considering.

## Link metrics reporting

Each measurement cycle logs and, when connected, publishes:

- `RSSI` from filtered IEEE 802.15.4 frame metadata
- `LQI` derived from the filtered RSSI value
- Zigbee transmit power (`TX power`, dBm) read from the Zigbee stack

`RSSI` and `LQI` are no longer taken from `esp_ieee802154_get_recent_*()`. The firmware now captures 802.15.4 frame info from:

- inbound unicast frames addressed to this device
- ACK frame metadata for outbound acknowledged unicast transmissions

This avoids reporting arbitrary “most recent frame” values from unrelated or stale traffic. If no fresh matching frame metadata has been seen recently, RSSI/LQI are reported as unavailable instead of publishing stale values. The serial log still shows the raw hardware `LQI` value for debugging, but the Zigbee `LastLQI` attribute is derived from filtered RSSI because the raw `LQI` on ESP32-C6 is not stable enough to expose directly.

Zigbee mapping on endpoint `10`:

- Diagnostics cluster (`0x0B05`)
  - `LastLQI` (`0x011C`)
  - `LastRSSI` (`0x011D`)
- Custom cluster (`0xFC00`)
  - `TX power dBm` (`0x0000`, signed 8-bit)

Additional ZHA-friendly sensor endpoints:

- endpoint `12`
  - Analog Input cluster
  - RSSI in dBm
- endpoint `13`
  - Analog Input cluster
  - TX power in dBm
