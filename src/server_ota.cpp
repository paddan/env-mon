#include "server_ota.h"
#include <Arduino.h>
#include <ArduinoLog.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>

WiFiClient client;

void checkForUpdates(const char *fw_image_url, const char *version_url, DeviceConfig cfg) {
    HTTPClient httpClient;
    int new_fw_version;

    Log.notice("Checking for firmware updates, current version is %d... " CR, cfg.fwVersion);

    httpClient.begin(client, version_url);
    int httpCode = httpClient.GET();
    if (httpCode == 200) {
        new_fw_version = httpClient.getString().toInt();
    } else {
        Log.error("version check failed, got HTTP response code %d" CR, httpCode);
        return;
    }
    httpClient.end();

    if (new_fw_version != cfg.fwVersion) {
        Log.notice("new version %d found!" CR, new_fw_version);

        httpUpdate.rebootOnUpdate(false);
        switch (httpUpdate.update(client, fw_image_url)) {
        case HTTP_UPDATE_OK:
            Log.notice("Update of firmware version %d to %d succeeded!" CR, cfg.fwVersion, new_fw_version);
            cfg.fwVersion = new_fw_version;
            cfg.save();
            break;
        case HTTP_UPDATE_FAILED:
            Log.error("HTTP_UPDATE_FAILD Error (%d): %s" CR, httpUpdate.getLastError(),
                      httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Log.warning("Found no update!" CR);
            break;
        }

        ESP.restart();
    } else {
        Log.notice("no new version found." CR);
    }
}
