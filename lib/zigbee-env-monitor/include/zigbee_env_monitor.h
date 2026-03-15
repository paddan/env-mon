#pragma once

#include <Arduino.h>

struct ZigbeeReportingConfig {
    uint8_t sensorEndpoint;
    uint8_t pressureEndpoint;
    uint8_t rssiEndpoint;
    uint8_t txPowerEndpoint;
    uint16_t reportMinIntervalS;
    uint16_t reportMaxIntervalS;
    float reportTempDeltaC;
    float reportHumidityDeltaPct;
    uint16_t reportPressureDeltaHpa;
    float reportRssiDeltaDbm;
    float reportTxPowerDeltaDbm;
    const char *manufacturer;
    const char *model;
};

struct ZigbeeJoinConfig {
    uint32_t zigbeeStartTimeoutMs;
    uint32_t rejoinTimeoutMs;
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
    void factoryResetNetwork();
    bool isStarted() const;

    // Resets RTC-persisted adaptive TX power state. Call when a firmware version sentinel mismatch
    // is detected to prevent stale learned values from carrying over after an update.
    static void resetAdaptiveTxPowerRtcState();

  private:
    void configureReporting();
    void applyStartupTxPower();
    void handleJoinWaitTxPower(uint32_t nowMs);
    bool setTxPowerDbm(int8_t txPowerDbm, const char *reason);

    ZigbeeReportingConfig reportingConfig_;
    ZigbeeJoinConfig joinConfig_;
    uint32_t lastJoinStatusLogMs_;
    uint32_t lastJoinPowerAdjustmentMs_;
    bool joinPowerRaisedThisAttempt_;
    bool reportingConfigured_;
    bool zigbeeStarted_;
};
