#ifndef _MQTT_H_
#define _MQTT_H_
#include <DeviceConfig.h>
#include <bme280.h>
#include <measurement.h>

void publishValues(Data data[], int numValues, int timeToPublishInSeconds, const DeviceConfig &cfg);

#endif
