# Environment monitor

Based on the ESP32 and BME280 temp/hummidity/pressure sensor.

## Core library

Configuration storage, Wi-Fi portal setup, and logging helpers now live in `lib/esp-core` as a reusable PlatformIO library (`DeviceConfig`, `wifi_handler`, `logging_utils`).

Sensor helpers are also split into reusable libraries:

- `lib/battery-sensor` – battery ADC helper
- `lib/bme280-sensor` – BME280 I2C helper

## ESP32-C3 SuperMini pinout

Select the `esp32c3_supermini` environment in `platformio.ini` to build for the smaller board. The default wiring assumes:

- BME280 uses SDA on GPIO_8 and SCL on GPIO_9 (override via `MONITOR_BME280_SDA_PIN`/`MONITOR_BME280_SCL_PIN` build flags if needed).
- Reset button is connected between GPIO_2 and GND (active-low) so it can be used as a wake-up source from deep sleep.
- Battery voltage divider uses GPIO_4 as the ADC input.

All of these pins can be changed without touching the source by adjusting the `MONITOR_*` defines in `platformio.ini` (see `include/BoardConfig.h` for the full list).

### ESP32-C3 SuperMini wiring (ASCII)

```ascii
ESP32-C3 SuperMini       BME280
------------------       ------
3V3                  ->  VIN
GND                  ->  GND
GPIO 8  (SDA)        ->  SDA
GPIO 9  (SCL)        ->  SCL
GPIO 2  (Reset btn)  ->  momentary switch  -> GND
GPIO 4  (ADC)        ->  1M/1M divider     -> Vbat
```

## Reset button

- GPIO_2 (active-low, button to GND)

## Battery level

Gnd - 1M - GPIO_4 - 1M - Vbat

## Factory reset

Hold the reset button (GPIO_2 on the ESP32-C3 SuperMini) for about two seconds while the device is awake or immediately after it wakes from deep sleep. The firmware will wipe the stored Wi-Fi/MQTT settings, restore the defaults, and reboot into the configuration portal.

### USB debug mode

Whenever the board detects an active USB serial connection (e.g., when you open `pio device monitor`), it automatically overrides the runtime schedule for quicker feedback:

- Measurements every **10 seconds**.
- MQTT publishes every **60 seconds** (up to 6 samples buffered).
- Verbose serial logging is enabled; when running on battery without USB the firmware drops to a quieter log level to save power.
- Each measurement prints a timestamped preview (local time + epoch) via `logging_utils`, which is handy when verifying sensor alignment before publishing.

These overrides are transient—they are not written to flash and normal intervals resume as soon as the USB cable or serial monitor is disconnected.

## MQTT configuration

The device stores its MQTT settings in flash. To change them, put the board into the Wi-Fi manager portal (hold the reset button until the `EnvMon-<mac>` access point shows up) and fill in:

- **Device** – friendly name exposed to Home Assistant.
- **MQTTHost / MQTTPort** – hostname/IP and TCP port of your broker (default `homeassistant.local:1883`).
- **MQTTTopic** – base topic prefix (default `env-mon/data`). The firmware automatically appends a sanitized device name segment so each device publishes to its own topic tree (for example `env-mon/data/living_room`).
- **MQTTUser / MQTTPwd** – required username/password for broker authentication.
- **MeasureInterval / PublishInterval** – durations in seconds that control how often the device samples the sensor and pushes data to MQTT. Publish intervals must be equal to or longer than the measurement interval. The firmware buffers up to 120 samples between publishes, so extremely large publish intervals will be clamped to stay within the RTC memory budget.

After saving, the device reboots, reconnects with the new credentials, and publishes to the configured topics on the next wake cycle.

## MQTT payloads

Each publish cycle sends retained messages to:

- `<baseTopic>/temperature`
- `<baseTopic>/humidity`
- `<baseTopic>/pressure`
- `<baseTopic>/battery_voltage`
- `<baseTopic>/rssi`

`<baseTopic>` is the effective per-device topic (`<configured MQTTTopic>/<sanitized deviceName>`), unless the configured topic already ends with that device segment.

Temperature, humidity, pressure, and battery voltage are averages computed across buffered samples from the previous publish window. RSSI is the current Wi-Fi signal strength at publish time.

Home Assistant discovery payloads are also published automatically to `homeassistant/sensor/<deviceId>/<sensor>/config`, so once the broker connection succeeds you should see one device with four entities appear under Settings → Devices & Services → MQTT.
