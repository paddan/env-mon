#ifndef _MQTT_H_
#define _MQTT_H_
#include <DeviceConfig.h>
#include <bme280.h>

typedef struct Data {
    float temperature;
    float humidity;
    float pressure;
    float batteryVoltage;
} Data;

void publishValues(Data data[], int numValues, int timeToPublishInSeconds, const DeviceConfig &cfg);

#endif
