#ifndef _MQTT_H_
#define _MQTT_H_
#include <DeviceConfig.h>
#include <bme280.h>

typedef struct Data {
    float temperature;
    float humidity;
    float pressure;
    float batteryVoltage;
    float pm1_0;
    float pm2_5;
    float pm10;
    bool hasPm;
} Data;

void publishValues(Data data[], int numValues, int timeToPublishInSeconds, const DeviceConfig &cfg);

#endif
