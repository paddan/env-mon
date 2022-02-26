#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <DeviceConfig.h>
#include <SPIFFS.h>

#define MAX_MESSAGE_SIZE 600

DeviceConfig::DeviceConfig() {
    if (!SPIFFS.begin(true)) {
        Log.error("An Error has occurred while mounting SPIFFS" CR);
        throw(-1);
    }
    File file = SPIFFS.open("/deviceConfig.json", "r");

    StaticJsonDocument<MAX_MESSAGE_SIZE> doc;
    deserializeJson(doc, file);

    SPIFFS.end();

    JsonObject obj = doc.as<JsonObject>();

    String _deviceName = obj["deviceName"].as<String>();
    int _fwVersion = obj["fwVersion"];
    String _mqttHost = obj["mqttHost"].as<String>();
    int _mqttPort = obj["mqttPort"];
    String _mqttTopic = obj["mqttTopic"].as<String>();

    if (!_deviceName.isEmpty())
        deviceName = _deviceName;
    if (!_mqttHost.isEmpty())
        mqttHost = _mqttHost;
    if (_mqttPort != 0)
        mqttPort = _mqttPort;
    if (!_mqttTopic.isEmpty())
        mqttTopic = _mqttTopic;
    fwVersion = _fwVersion;
}

void DeviceConfig::save() {
    if (!SPIFFS.begin(true)) {
        Log.error("An Error has occurred while mounting SPIFFS" CR);
        throw(-1);
    }
    File file = SPIFFS.open("/deviceConfig.json", "w");

    StaticJsonDocument<MAX_MESSAGE_SIZE> doc;

    doc["deviceName"] = deviceName;
    doc["fwVersion"] = fwVersion;
    doc["mqttHost"] = mqttHost;
    doc["mqttPort"] = mqttPort;
    doc["mqttTopic"] = mqttTopic;

    serializeJson(doc, file);

    file.flush();
    SPIFFS.end();
}
