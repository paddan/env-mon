#pragma once

#include <Arduino.h>

struct ZigbeeReportingConfig {
    uint8_t sensorEndpoint;
    uint8_t pressureEndpoint;
    uint16_t reportMinIntervalS;
    uint16_t reportMaxIntervalS;
    float reportTempDeltaC;
    float reportHumidityDeltaPct;
    uint16_t reportPressureDeltaHpa;
    const char *manufacturer;
    const char *model;
};

struct ZigbeeJoinConfig {
    uint32_t zigbeeStartTimeoutMs;
    uint32_t joinStatusLogIntervalMs;
};

using FactoryResetCheckFn = bool (*)();

class ZigbeeEnvMonitor {
public:
    ZigbeeEnvMonitor(const ZigbeeReportingConfig &reportingConfig, const ZigbeeJoinConfig &joinConfig);

    void addAndConfigureEndpoints();
    bool beginEndDevice();
    bool waitForNetworkJoin(FactoryResetCheckFn factoryResetCheck);
    void publishMeasurement();

private:
    void configureReporting();

    ZigbeeReportingConfig reportingConfig_;
    ZigbeeJoinConfig joinConfig_;
    uint32_t lastJoinStatusLogMs_;
    bool reportingConfigured_;
    bool zigbeeStarted_;
};
