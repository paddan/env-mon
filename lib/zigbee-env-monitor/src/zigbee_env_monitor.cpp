#include "zigbee_env_monitor.h"
#include "BoardConfig.h"
#include <Arduino.h>
#include <Zigbee.h>
#include <ep/ZigbeeAnalog.h>
#include <battery.h>
#include <bme280.h>
#include <cstdio>
#include <cstring>
#include <esp_ieee802154.h>
#include <esp_log.h>
#include <esp_zigbee_attribute.h>
#include <esp_zigbee_cluster.h>
#include <esp_zigbee_core.h>
#include <math.h>
#include <new>
#include <zcl/esp_zigbee_zcl_common.h>
#include <zcl/esp_zigbee_zcl_diagnostics.h>

#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is required. Add -DZIGBEE_MODE_ED in build flags."
#endif

namespace {
constexpr char TAG[] = "zigbee-env";
constexpr float TEMPERATURE_MIN_C = -40.0f;
constexpr float TEMPERATURE_MAX_C = 85.0f;
constexpr float HUMIDITY_MIN_PCT = 0.0f;
constexpr float HUMIDITY_MAX_PCT = 100.0f;
constexpr float TEMPERATURE_TOLERANCE_C = 0.1f;
constexpr float HUMIDITY_TOLERANCE_PCT = 0.1f;
constexpr float HUMIDITY_INITIAL_PCT = 50.0f;
constexpr int16_t PRESSURE_MIN_HPA = 300;
constexpr int16_t PRESSURE_MAX_HPA = 1100;
constexpr int16_t PRESSURE_TOLERANCE_HPA = 1;
constexpr size_t ZCL_MAX_TEXT_LENGTH = 32;
constexpr float BATTERY_MIN_V = 3.0f;
constexpr float BATTERY_MAX_V = 4.2f;
constexpr uint16_t CUSTOM_TX_POWER_CLUSTER_ID = 0xFC00;
constexpr uint16_t CUSTOM_TX_POWER_ATTR_ID = 0x0000;
constexpr float RSSI_MIN_DBM = -127.0f;
constexpr float RSSI_MAX_DBM = 20.0f;
constexpr float TX_POWER_MIN_DBM = -40.0f;
constexpr float TX_POWER_MAX_DBM = 40.0f;

bool shouldSendExplicitReports() {
#if MONITOR_ZIGBEE_EXPLICIT_REPORTS
    return true;
#else
    return false;
#endif
}

uint8_t batteryPercentFromVoltage(float voltage) {
    if (!isfinite(voltage)) {
        return 0;
    }
    if (voltage <= BATTERY_MIN_V) {
        return 0;
    }
    if (voltage >= BATTERY_MAX_V) {
        return 100;
    }
    return static_cast<uint8_t>(lroundf(((voltage - BATTERY_MIN_V) * 100.0f) / (BATTERY_MAX_V - BATTERY_MIN_V)));
}

uint8_t batteryDeciVolts(float voltage) {
    const float deciVolts = lroundf(voltage * 10.0f);
    if (!isfinite(deciVolts) || deciVolts < 0.0f) {
        return 0;
    }
    if (deciVolts > 255.0f) {
        return 255;
    }
    return static_cast<uint8_t>(deciVolts);
}

void configureZigbeeIdentity(char *manufacturerOut, size_t manufacturerSize, char *modelOut, size_t modelSize,
                             const char *manufacturer, const char *model) {
    const char *manufacturerValue = manufacturer != nullptr ? manufacturer : "EnvMon";
    const char *modelValue = model != nullptr ? model : "C6-BME280";

    snprintf(manufacturerOut, manufacturerSize, "%.*s", static_cast<int>(ZCL_MAX_TEXT_LENGTH), manufacturerValue);

    // Keep a short stable identifier derived from eFuse MAC so identical firmware images are still distinguishable.
    const uint32_t shortChipId = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFULL);
    const char *placeholder = strstr(modelValue, "XXXXX");
    if (placeholder != nullptr) {
        const size_t prefixLen = static_cast<size_t>(placeholder - modelValue);
        const char *suffix = placeholder + strlen("XXXXX");
        char expandedModel[64];
        snprintf(expandedModel, sizeof(expandedModel), "%.*s%05lX%s", static_cast<int>(prefixLen), modelValue,
                 static_cast<unsigned long>(shortChipId), suffix);
        snprintf(modelOut, modelSize, "%.*s", static_cast<int>(ZCL_MAX_TEXT_LENGTH), expandedModel);
        return;
    }

    constexpr size_t suffixLength = 1 + 5; // "-XXXXX"
    constexpr size_t maxBaseLength = ZCL_MAX_TEXT_LENGTH - suffixLength;
    snprintf(modelOut, modelSize, "%.*s-%05lX", static_cast<int>(maxBaseLength), modelValue,
             static_cast<unsigned long>(shortChipId));
}
} // namespace

class ZigbeeTempSensorWithLinkMetrics : public ZigbeeTempSensor {
public:
    explicit ZigbeeTempSensorWithLinkMetrics(uint8_t endpoint)
        : ZigbeeTempSensor(endpoint), lastLqi_(ESP_ZB_ZCL_DIAGNOSTICS_LAST_LQI_DEFAULT_VALUE),
          lastRssi_(ESP_ZB_ZCL_DIAGNOSTICS_LAST_RSSI_DEFAULT_VALUE), txPowerDbm_(0), metricsReady_(false) {}

    bool addLinkMetricClusters() {
        if (metricsReady_) {
            return true;
        }

        esp_zb_diagnostics_cluster_cfg_t diagnosticsCfg = {};
        esp_zb_attribute_list_t *diagnosticsCluster = esp_zb_diagnostics_cluster_create(&diagnosticsCfg);
        if (diagnosticsCluster == nullptr) {
            ESP_LOGE(TAG, "Failed to create diagnostics cluster");
            return false;
        }

        if (!addDiagAttr(diagnosticsCluster, ESP_ZB_ZCL_ATTR_DIAGNOSTICS_LAST_LQI_ID, &lastLqi_)) {
            return false;
        }
        if (!addDiagAttr(diagnosticsCluster, ESP_ZB_ZCL_ATTR_DIAGNOSTICS_LAST_RSSI_ID, &lastRssi_)) {
            return false;
        }

        esp_err_t ret = esp_zb_cluster_list_add_diagnostics_cluster(_cluster_list, diagnosticsCluster,
                                                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add diagnostics cluster: 0x%x", ret);
            return false;
        }

        esp_zb_attribute_list_t *txPowerCluster = esp_zb_zcl_attr_list_create(CUSTOM_TX_POWER_CLUSTER_ID);
        if (txPowerCluster == nullptr) {
            ESP_LOGE(TAG, "Failed to create custom tx-power cluster");
            return false;
        }

        ret = esp_zb_custom_cluster_add_custom_attr(
            txPowerCluster, CUSTOM_TX_POWER_ATTR_ID, ESP_ZB_ZCL_ATTR_TYPE_S8,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &txPowerDbm_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add custom tx-power attribute: 0x%x", ret);
            return false;
        }

        ret = esp_zb_cluster_list_add_custom_cluster(_cluster_list, txPowerCluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add custom tx-power cluster: 0x%x", ret);
            return false;
        }

        metricsReady_ = true;
        return true;
    }

    bool setLastLqi(uint8_t lqi) {
        if (!metricsReady_) {
            return false;
        }
        lastLqi_ = lqi;
        return setAttr(ESP_ZB_ZCL_CLUSTER_ID_DIAGNOSTICS, ESP_ZB_ZCL_ATTR_DIAGNOSTICS_LAST_LQI_ID, &lastLqi_);
    }

    bool setLastRssi(int8_t rssiDbm) {
        if (!metricsReady_) {
            return false;
        }
        lastRssi_ = rssiDbm;
        return setAttr(ESP_ZB_ZCL_CLUSTER_ID_DIAGNOSTICS, ESP_ZB_ZCL_ATTR_DIAGNOSTICS_LAST_RSSI_ID, &lastRssi_);
    }

    bool setTxPowerDbm(int8_t txPowerDbm) {
        if (!metricsReady_) {
            return false;
        }
        txPowerDbm_ = txPowerDbm;
        return setAttr(CUSTOM_TX_POWER_CLUSTER_ID, CUSTOM_TX_POWER_ATTR_ID, &txPowerDbm_);
    }

    bool reportLastLqi() {
        return metricsReady_ && reportAttr(ESP_ZB_ZCL_CLUSTER_ID_DIAGNOSTICS, ESP_ZB_ZCL_ATTR_DIAGNOSTICS_LAST_LQI_ID);
    }

    bool reportLastRssi() {
        return metricsReady_ && reportAttr(ESP_ZB_ZCL_CLUSTER_ID_DIAGNOSTICS, ESP_ZB_ZCL_ATTR_DIAGNOSTICS_LAST_RSSI_ID);
    }

    bool reportTxPowerDbm() {
        return metricsReady_ && reportAttr(CUSTOM_TX_POWER_CLUSTER_ID, CUSTOM_TX_POWER_ATTR_ID);
    }

private:
    bool addDiagAttr(esp_zb_attribute_list_t *cluster, uint16_t attrId, void *value) {
        const esp_err_t ret = esp_zb_diagnostics_cluster_add_attr(cluster, attrId, value);
        if (ret == ESP_OK) {
            return true;
        }
        ESP_LOGE(TAG, "Failed adding diagnostics attribute 0x%04x: 0x%x", attrId, ret);
        return false;
    }

    bool setAttr(uint16_t clusterId, uint16_t attrId, void *value) {
        esp_zb_lock_acquire(portMAX_DELAY);
        const esp_zb_zcl_status_t status =
            esp_zb_zcl_set_attribute_val(getEndpoint(), clusterId, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attrId, value, false);
        esp_zb_lock_release();
        if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "Failed setting cluster=0x%04x attr=0x%04x status=0x%x", clusterId, attrId, status);
            return false;
        }
        return true;
    }

    bool reportAttr(uint16_t clusterId, uint16_t attrId) {
        esp_zb_zcl_report_attr_cmd_t report = {};
        report.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
        report.attributeID = attrId;
        report.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
        report.clusterID = clusterId;
        report.zcl_basic_cmd.src_endpoint = getEndpoint();
        report.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;

        esp_zb_lock_acquire(portMAX_DELAY);
        const esp_err_t ret = esp_zb_zcl_report_attr_cmd_req(&report);
        esp_zb_lock_release();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed reporting cluster=0x%04x attr=0x%04x err=0x%x", clusterId, attrId, ret);
            return false;
        }
        return true;
    }

    uint8_t lastLqi_;
    int8_t lastRssi_;
    int8_t txPowerDbm_;
    bool metricsReady_;
};

class ZigbeeRuntime {
public:
    ZigbeeRuntime(uint8_t sensorEndpoint, uint8_t pressureEndpoint, uint8_t rssiEndpoint, uint8_t txPowerEndpoint)
        : tempSensor(sensorEndpoint), pressureSensor(pressureEndpoint), rssiSensor(rssiEndpoint), txPowerSensor(txPowerEndpoint) {}

    ZigbeeTempSensorWithLinkMetrics tempSensor;
    ZigbeePressureSensor pressureSensor;
    ZigbeeAnalog rssiSensor;
    ZigbeeAnalog txPowerSensor;
    Bme280Sensor bme;
    char manufacturerIdentity[ZCL_MAX_TEXT_LENGTH + 1];
    char modelIdentity[ZCL_MAX_TEXT_LENGTH + 1];
};

alignas(ZigbeeRuntime) static uint8_t runtimeStorage[sizeof(ZigbeeRuntime)];
static bool runtimeInitialized = false;

ZigbeeEnvMonitor::ZigbeeEnvMonitor(const ZigbeeReportingConfig &reportingConfig, const ZigbeeJoinConfig &joinConfig)
    : reportingConfig_(reportingConfig), joinConfig_(joinConfig), lastJoinStatusLogMs_(0), reportingConfigured_(false),
      zigbeeStarted_(false) {}

static ZigbeeRuntime &getRuntime(const ZigbeeReportingConfig &reportingConfig) {
    if (!runtimeInitialized) {
        new (runtimeStorage)
            ZigbeeRuntime(reportingConfig.sensorEndpoint, reportingConfig.pressureEndpoint, reportingConfig.rssiEndpoint,
                          reportingConfig.txPowerEndpoint);
        runtimeInitialized = true;
    }
    return *reinterpret_cast<ZigbeeRuntime *>(runtimeStorage);
}

void ZigbeeEnvMonitor::addAndConfigureEndpoints() {
    ZigbeeRuntime &r = getRuntime(reportingConfig_);
    configureZigbeeIdentity(r.manufacturerIdentity, sizeof(r.manufacturerIdentity), r.modelIdentity,
                            sizeof(r.modelIdentity), reportingConfig_.manufacturer, reportingConfig_.model);
    ESP_LOGI(TAG, "Zigbee identity: manufacturer='%s', model='%s'", r.manufacturerIdentity, r.modelIdentity);

    r.tempSensor.setManufacturerAndModel(r.manufacturerIdentity, r.modelIdentity);
    r.tempSensor.setPowerSource(ZB_POWER_SOURCE_BATTERY, 100, 42);
    r.tempSensor.setMinMaxValue(TEMPERATURE_MIN_C, TEMPERATURE_MAX_C);
    r.tempSensor.setTolerance(TEMPERATURE_TOLERANCE_C);
    r.tempSensor.addHumiditySensor(HUMIDITY_MIN_PCT, HUMIDITY_MAX_PCT, HUMIDITY_TOLERANCE_PCT, HUMIDITY_INITIAL_PCT);
    if (!r.tempSensor.addLinkMetricClusters()) {
        ESP_LOGW(TAG, "Link metric clusters could not be configured; RSSI/LQI/TX-power Zigbee reporting will be disabled");
    }

    r.pressureSensor.setManufacturerAndModel(r.manufacturerIdentity, r.modelIdentity);
    r.pressureSensor.setMinMaxValue(PRESSURE_MIN_HPA, PRESSURE_MAX_HPA);
    r.pressureSensor.setTolerance(PRESSURE_TOLERANCE_HPA);

    r.rssiSensor.setManufacturerAndModel(r.manufacturerIdentity, r.modelIdentity);
    r.rssiSensor.addAnalogInput();
    r.rssiSensor.setAnalogInputApplication(ESP_ZB_ZCL_AI_SET_APP_TYPE_WITH_ID(ESP_ZB_ZCL_AI_APP_TYPE_OTHER, 0x1001));
    r.rssiSensor.setAnalogInputDescription("RSSI (dBm)");
    r.rssiSensor.setAnalogInputResolution(1.0f);
    r.rssiSensor.setAnalogInputMinMax(RSSI_MIN_DBM, RSSI_MAX_DBM);

    r.txPowerSensor.setManufacturerAndModel(r.manufacturerIdentity, r.modelIdentity);
    r.txPowerSensor.addAnalogInput();
    r.txPowerSensor.setAnalogInputApplication(ESP_ZB_ZCL_AI_SET_APP_TYPE_WITH_ID(ESP_ZB_ZCL_AI_APP_TYPE_OTHER, 0x1002));
    r.txPowerSensor.setAnalogInputDescription("TX Power (dBm)");
    r.txPowerSensor.setAnalogInputResolution(1.0f);
    r.txPowerSensor.setAnalogInputMinMax(TX_POWER_MIN_DBM, TX_POWER_MAX_DBM);

    ESP_LOGI(TAG, "Adding Zigbee temperature/humidity, pressure, RSSI and TX power endpoints");
    Zigbee.addEndpoint(&r.tempSensor);
    Zigbee.addEndpoint(&r.pressureSensor);
    Zigbee.addEndpoint(&r.rssiSensor);
    Zigbee.addEndpoint(&r.txPowerSensor);
}

bool ZigbeeEnvMonitor::beginEndDevice() {
    Zigbee.setTimeout(joinConfig_.zigbeeStartTimeoutMs);
    if (!Zigbee.begin(ZIGBEE_END_DEVICE)) {
        zigbeeStarted_ = false;
        return false;
    }

    zigbeeStarted_ = true;
    configureReporting();
    return true;
}

void ZigbeeEnvMonitor::configureReporting() {
    if (reportingConfigured_) {
        return;
    }

    ZigbeeRuntime &r = getRuntime(reportingConfig_);
    r.tempSensor.setReporting(reportingConfig_.reportMinIntervalS, reportingConfig_.reportMaxIntervalS,
                              reportingConfig_.reportTempDeltaC);
    r.tempSensor.setHumidityReporting(reportingConfig_.reportMinIntervalS, reportingConfig_.reportMaxIntervalS,
                                      reportingConfig_.reportHumidityDeltaPct);
    r.pressureSensor.setReporting(reportingConfig_.reportMinIntervalS, reportingConfig_.reportMaxIntervalS,
                                  reportingConfig_.reportPressureDeltaHpa);
    r.rssiSensor.setAnalogInputReporting(reportingConfig_.reportMinIntervalS, reportingConfig_.reportMaxIntervalS,
                                         reportingConfig_.reportRssiDeltaDbm);
    r.txPowerSensor.setAnalogInputReporting(reportingConfig_.reportMinIntervalS, reportingConfig_.reportMaxIntervalS,
                                            reportingConfig_.reportTxPowerDeltaDbm);
    reportingConfigured_ = true;
}

bool ZigbeeEnvMonitor::waitForNetworkJoin(FactoryResetCheckFn factoryResetCheck) {
    if (!zigbeeStarted_) {
        ESP_LOGW(TAG, "Zigbee stack not started; skipping network join wait");
        return false;
    }

    ESP_LOGI(TAG, "Waiting for Zigbee network join");
    const uint32_t joinStartMs = millis();
    lastJoinStatusLogMs_ = joinStartMs;

    while (!Zigbee.connected()) {
        if (factoryResetCheck != nullptr && factoryResetCheck()) {
            ESP_LOGW(TAG, "Factory reset requested before join");
            Zigbee.factoryReset();
        }

        const uint32_t now = millis();
        if ((now - lastJoinStatusLogMs_) >= joinConfig_.joinStatusLogIntervalMs) {
            ESP_LOGI(TAG, "Still waiting for Zigbee network join");
            lastJoinStatusLogMs_ = now;
        }
        if ((now - joinStartMs) >= joinConfig_.zigbeeStartTimeoutMs) {
            ESP_LOGW(TAG, "Zigbee join timed out after %lu ms; continuing with local logging only",
                     static_cast<unsigned long>(joinConfig_.zigbeeStartTimeoutMs));
            return false;
        }
        delay(250);
    }

    ESP_LOGI(TAG, "Zigbee connected");
    return true;
}

void ZigbeeEnvMonitor::publishMeasurement() {
    ZigbeeRuntime &r = getRuntime(reportingConfig_);
    const float batteryVoltage = readBatteryVoltage(MONITOR_BATTERY_PIN);
    const bool batteryValid = isfinite(batteryVoltage) && batteryVoltage > 0.0f;
    const bool zigbeeConnected = zigbeeStarted_ && Zigbee.connected();
    const bool linkMetricsAvailable = zigbeeStarted_;
    uint8_t recentLqi = 0;
    int8_t recentRssiDbm = 0;
    int8_t txPowerDbm = 0;
    if (linkMetricsAvailable) {
        recentLqi = esp_ieee802154_get_recent_lqi();
        recentRssiDbm = esp_ieee802154_get_recent_rssi();
        esp_zb_get_tx_power(&txPowerDbm);
    }

    r.bme.measure();
    if (!r.bme.available || !isfinite(r.bme.temperature) || !isfinite(r.bme.humidity) || !isfinite(r.bme.pressure)) {
        ESP_LOGW(TAG, "Invalid measurement sample (bmeAvailable=%d battery=%.2f V zigbeeConnected=%d)", r.bme.available,
                 batteryVoltage, zigbeeConnected);
        return;
    }

    const int16_t pressureHpa = static_cast<int16_t>(lroundf(r.bme.pressure));
    uint8_t batteryPct = 0;
    if (batteryValid) {
        batteryPct = batteryPercentFromVoltage(batteryVoltage);
    }

    if (linkMetricsAvailable) {
        ESP_LOGI(
            TAG,
            "Measurement: temp=%.2f C, humidity=%.2f %%, pressure=%d hPa, battery=%.2f V (%u%%), rssi=%d dBm, lqi=%u, txPower=%d dBm, zigbeeConnected=%d",
            r.bme.temperature, r.bme.humidity, pressureHpa, batteryVoltage, batteryPct, recentRssiDbm, recentLqi,
            txPowerDbm, zigbeeConnected);
    } else {
        ESP_LOGI(
            TAG,
            "Measurement: temp=%.2f C, humidity=%.2f %%, pressure=%d hPa, battery=%.2f V (%u%%), rssi=n/a, lqi=n/a, txPower=n/a, zigbeeConnected=%d",
            r.bme.temperature, r.bme.humidity, pressureHpa, batteryVoltage, batteryPct, zigbeeConnected);
    }

    if (!zigbeeConnected) {
        return;
    }

    const bool tempSet = r.tempSensor.setTemperature(r.bme.temperature);
    const bool humSet = r.tempSensor.setHumidity(r.bme.humidity);
    const bool pressureSet = r.pressureSensor.setPressure(pressureHpa);
    bool rssiSet = false;
    bool lqiSet = false;
    bool txPowerSet = false;
    bool rssiAnalogSet = false;
    bool txPowerAnalogSet = false;
    if (linkMetricsAvailable) {
        rssiSet = r.tempSensor.setLastRssi(recentRssiDbm);
        lqiSet = r.tempSensor.setLastLqi(recentLqi);
        txPowerSet = r.tempSensor.setTxPowerDbm(txPowerDbm);
        rssiAnalogSet = r.rssiSensor.setAnalogInput(static_cast<float>(recentRssiDbm));
        txPowerAnalogSet = r.txPowerSensor.setAnalogInput(static_cast<float>(txPowerDbm));
    }
    bool batteryPctSet = false;
    bool batteryVoltSet = false;
    bool batteryReported = false;
    uint8_t batteryDv = 0;
    if (batteryValid) {
        batteryDv = batteryDeciVolts(batteryVoltage);
        batteryPctSet = r.tempSensor.setBatteryPercentage(batteryPct);
        batteryVoltSet = r.tempSensor.setBatteryVoltage(batteryDv);
    }
    bool rssiReported = false;
    bool lqiReported = false;
    bool txPowerReported = false;
    bool tempHumidityReported = false;
    bool pressureReported = false;
    bool rssiAnalogReported = false;
    bool txPowerAnalogReported = false;

    if (shouldSendExplicitReports()) {
        if (batteryValid) {
            batteryReported = r.tempSensor.reportBatteryPercentage();
        }
        tempHumidityReported = r.tempSensor.report();
        pressureReported = r.pressureSensor.report();
        if (linkMetricsAvailable) {
            rssiReported = r.tempSensor.reportLastRssi();
            lqiReported = r.tempSensor.reportLastLqi();
            txPowerReported = r.tempSensor.reportTxPowerDbm();
            rssiAnalogReported = r.rssiSensor.reportAnalogInput();
            txPowerAnalogReported = r.txPowerSensor.reportAnalogInput();
        }
    }

    ESP_LOGI(TAG,
             "Zigbee attribute update: temp=%.2f C, humidity=%.2f %%, pressure=%d hPa, battery=%.2f V (%u%%), "
             "rssi=%d dBm, lqi=%u, txPower=%d dBm, explicitReports=%d "
             "(setT=%d setH=%d setP=%d setRSSI=%d setLQI=%d setTxPower=%d setRSSIAnalog=%d setTxPowerAnalog=%d "
             "setBattPct=%d setBattV=%d reportTH=%d reportP=%d reportRSSI=%d reportLQI=%d reportTxPower=%d "
             "reportRSSIAnalog=%d reportTxPowerAnalog=%d reportBatt=%d)",
             r.bme.temperature, r.bme.humidity, pressureHpa, batteryVoltage, batteryPct, recentRssiDbm, recentLqi,
             txPowerDbm, shouldSendExplicitReports(), tempSet, humSet, pressureSet, rssiSet, lqiSet, txPowerSet,
             rssiAnalogSet, txPowerAnalogSet, batteryPctSet, batteryVoltSet, tempHumidityReported, pressureReported,
             rssiReported, lqiReported, txPowerReported, rssiAnalogReported, txPowerAnalogReported, batteryReported);
}

void ZigbeeEnvMonitor::factoryResetNetwork() {
    if (!zigbeeStarted_) {
        ESP_LOGW(TAG, "Zigbee stack not started; skipping network factory reset");
        return;
    }

    ESP_LOGW(TAG, "Resetting Zigbee network state and restarting join");
    Zigbee.factoryReset();
}

bool ZigbeeEnvMonitor::isStarted() const {
    return zigbeeStarted_;
}
