#include "mqtt.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <sys/time.h>
#include <time.h>

RTC_DATA_ATTR int numMessagesPublished = 0;

#define MAX_MESSAGE_SIZE 1024
#define NTP_SERVER "europe.pool.ntp.org"

void publishValues(Data *data, int numValues, int timeToPublishInSeconds, DeviceConfig cfg) {
    WiFiClient wifiClient;
    PubSubClient client(wifiClient);
    client.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
    client.setBufferSize(MAX_MESSAGE_SIZE);

    while (!client.connected() && !wifiClient.connected()) {
        if (!client.connect(cfg.deviceName.c_str())) {
            Log.error("Not connected to %s:%d... retrying!" CR, cfg.mqttHost.c_str(), cfg.mqttPort);
            delay(1000);
        }
    }

    configTime(3600, 3600, NTP_SERVER);
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo)) {
        Log.error("Failed to obtain time" CR);
        return;
    }

    long time = mktime(&timeInfo) - timeToPublishInSeconds;
    long timeBetweenMeasurements = timeToPublishInSeconds / numValues;

    char json[MAX_MESSAGE_SIZE];
    StaticJsonDocument<MAX_MESSAGE_SIZE> doc;

    doc["name"] = cfg.deviceName;
    doc["ver"] = cfg.fwVersion;
    doc["seq"] = numMessagesPublished++;

    for (size_t i = 0; i < numValues; i++) {
        doc["data"][i]["epoch"] = time;
        doc["data"][i]["temp"] = data[i].temperature;
        doc["data"][i]["hum"] = data[i].humidity;
        doc["data"][i]["pres"] = data[i].pressure;
        doc["data"][i]["bat"] = data[i].batteryVoltage;
        time += timeBetweenMeasurements;
    }

    size_t size = serializeJson(doc, json, MAX_MESSAGE_SIZE);

    if (!client.publish("env-mon/data", json, size)) {
        Log.error("Failed to publish!" CR);
        return;
    }
    Log.notice("Published %s to %s:%d" CR, json, cfg.mqttHost.c_str(), cfg.mqttPort);

    client.disconnect();
}
