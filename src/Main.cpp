#include "battery.h"
#include "bme280.h"
#include "driver/adc.h"
#include "mqtt.h"
#include "server_ota.h"
#include "wifi_handler.h"
#include <Arduino.h>
#include <ArduinoLog.h>
#include <DeviceConfig.h>
#include <PubSubClient.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_wifi.h>

#define RESET_PIN GPIO_NUM_14
#define BATTERY_PIN GPIO_NUM_39

#define FW_IMAGE_URL "http://toadstation.local:6060/esp32firmware/environment-monitor/firmware.bin"
#define VERSION_URL "http://toadstation.local:6060/esp32firmware/environment-monitor/version.txt"

#define uS_TO_S_FACTOR 1000000LLU                         /* Conversion factor for micro seconds to seconds */
#define TIME_TO_MEASURE 600                               /* Time between measurments (in seconds) */
#define TIME_TO_PUBLISH 8 * TIME_TO_MEASURE               /* Time between publishing to MQTT (in seconds) */
#define TIME_TO_UPDATE 24 * TIME_TO_PUBLISH               /* Time between update checks (in seconds) */
#define NUM_MEASURMENTS TIME_TO_PUBLISH / TIME_TO_MEASURE /* Number of measurments before publishing */

#define DISABLE_LOGGING 1

RTC_DATA_ATTR int publishCount = 0;
RTC_DATA_ATTR int updateCount = 0;
RTC_DATA_ATTR Data data[NUM_MEASURMENTS];

void setup() {
    Serial.begin(115200);

    Log.begin(LOG_LEVEL_VERBOSE, &Serial);

    esp_bluedroid_disable();
    esp_bt_controller_disable();

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        Log.warning("Resetting wifi!" CR);
        resetWifi();
    }

    Bme280Sensor bme;

    float batteryVoltage = readBatteryVoltage(BATTERY_PIN);

    bme.measure();
    data[publishCount++] = Data{bme.temperature, bme.humidity, bme.pressure, batteryVoltage};

    DeviceConfig cfg;

    if (updateCount == 0 || updateCount * TIME_TO_MEASURE >= TIME_TO_UPDATE) {
        connectToRouter(cfg);
        checkForUpdates(FW_IMAGE_URL, VERSION_URL, cfg);
        updateCount = 0;
    }
    updateCount++;

    if (publishCount * TIME_TO_MEASURE >= TIME_TO_PUBLISH) {
        connectToRouter(cfg);
        publishValues(data, NUM_MEASURMENTS, TIME_TO_PUBLISH, cfg);
        publishCount = 0;
    }

    pinMode(RESET_PIN, INPUT_PULLDOWN);
    esp_sleep_enable_ext0_wakeup(RESET_PIN, HIGH);
    esp_sleep_enable_timer_wakeup(TIME_TO_MEASURE * uS_TO_S_FACTOR);

    Log.notice("Going to sleep for %d seconds" CR, TIME_TO_MEASURE);

    esp_wifi_stop();
    esp_deep_sleep_start();
}

void loop() {}
