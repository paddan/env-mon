#ifndef _WIFI_HANDLER_H_
#define _WIFI_HANDLER_H_
#include <DeviceConfig.h>

void connectToRouter(DeviceConfig &cfg, bool forcePortal = false);
void resetWifi();

#endif
