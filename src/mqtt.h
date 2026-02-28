#ifndef _MQTT_H_
#define _MQTT_H_
#include <DeviceConfig.h>
#include <measurement.h>

bool publishValues(Data data[], int numValues, int timeToPublishInSeconds, const DeviceConfig &cfg);

#endif
