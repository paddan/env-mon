# Environment monitor

Based on the ESP32 and BME280 temp/hummidity/pressure sensor. Firmware version checking and download is done from toadstation.local:6060.

- "http://toadstation.local:6060/esp32firmware/environment-monitor/firmware.bin"
- "http://toadstation.local:6060/esp32firmware/environment-monitor/version.txt"

## BME280 pintout

- VIN -> 3.3
- GND -> GND
- SDA -> GPIO_21
- SCL -> GPIO_22

## Reset button

- GPIO_14

## Battery level

Gnd - 1M - GPIO_39 - 1M - Vbat

## TODO

1. Store version permanently :heavy_check_mark:
1. Add WifiManager :heavy_check_mark:
1. Make firmware check run once a day and at reboot :heavy_check_mark:
1. Add button to reset device and run WifiManager :heavy_check_mark:
1. Store measurements locally and publish every hour or so :heavy_check_mark:
1. Long press on reset button should reset wifimanager, short press should check for new firmware
1. Set up a MQTT server on synology :heavy_check_mark:
1. Setup MQTT and publish data :heavy_check_mark:
1. Add name on each device via wifi manager configuration (store in flash) :heavy_check_mark:
1. Configure a node-red flow to send data to influxdb :heavy_check_mark:
1. Setup grafana to visualize data :heavy_check_mark:
1. Add MQTT configuration (server and port) :heavy_check_mark:
1. Figure out how to meassure battery level correctly :heavy_check_mark:
1. Add time of publish :heavy_check_mark:
1. Add a good logging library :heavy_check_mark:
