#ifndef _SERVER_OTA_H_
#define _SERVER_OTA_H_

#include "DeviceConfig.h"

void checkForUpdates(const char *fw_image_url, const char *version_url, DeviceConfig cfg);
int getFirmwareVersion();

#endif
