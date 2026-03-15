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
#include <esp_timer.h>
#include <esp_zigbee_attribute.h>
#include <esp_zigbee_cluster.h>
#include <esp_zigbee_core.h>
#include <freertos/FreeRTOS.h>
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
constexpr int8_t LQI_RSSI_FLOOR_DBM = -80;
constexpr int8_t LQI_RSSI_CEILING_DBM = -30;
constexpr float TX_POWER_MIN_DBM = -40.0f;
constexpr float TX_POWER_MAX_DBM = 40.0f;
constexpr int8_t ADAPTIVE_TX_POWER_MIN_DBM = MONITOR_ZIGBEE_TX_POWER_MIN_DBM;
constexpr int8_t ADAPTIVE_TX_POWER_MAX_DBM = MONITOR_ZIGBEE_TX_POWER_MAX_DBM;
constexpr int8_t ADAPTIVE_TX_POWER_STEP_DBM = MONITOR_ZIGBEE_TX_POWER_STEP_DBM;
constexpr uint8_t ADAPTIVE_TX_POWER_SUCCESSFUL_JOIN_COUNT = MONITOR_ZIGBEE_TX_POWER_SUCCESSFUL_JOIN_COUNT;
constexpr uint32_t LINK_METRICS_MAX_AGE_MS = MONITOR_ZIGBEE_LINK_METRICS_MAX_AGE_MS;
constexpr uint16_t IEEE802154_BROADCAST_SHORT_ADDR = 0xFFFF;

#if MONITOR_ZIGBEE_TX_POWER_STEP_DBM < 1
#error "MONITOR_ZIGBEE_TX_POWER_STEP_DBM must be >= 1"
#endif

#if MONITOR_ZIGBEE_TX_POWER_MIN_DBM > MONITOR_ZIGBEE_TX_POWER_MAX_DBM
#error "MONITOR_ZIGBEE_TX_POWER_MIN_DBM must be <= MONITOR_ZIGBEE_TX_POWER_MAX_DBM"
#endif

#if MONITOR_ZIGBEE_TX_POWER_SUCCESSFUL_JOIN_COUNT < 1
#error "MONITOR_ZIGBEE_TX_POWER_SUCCESSFUL_JOIN_COUNT must be >= 1"
#endif

#if MONITOR_ZIGBEE_LINK_METRICS_MAX_AGE_MS < 1
#error "MONITOR_ZIGBEE_LINK_METRICS_MAX_AGE_MS must be >= 1"
#endif

#if MONITOR_ZIGBEE_REJOIN_TIMEOUT_MS < 1
#error "MONITOR_ZIGBEE_REJOIN_TIMEOUT_MS must be >= 1"
#endif

RTC_DATA_ATTR bool adaptiveTxPowerInitialized = false;
RTC_DATA_ATTR int8_t adaptivePreferredTxPowerDbm = ADAPTIVE_TX_POWER_MIN_DBM;
RTC_DATA_ATTR uint8_t adaptiveSuccessfulJoinCount = 0;

enum class MacAddressMode : uint8_t {
    None = 0,
    Reserved = 1,
    Short = 2,
    Extended = 3,
};

struct ParsedMacHeader {
    bool valid;
    MacAddressMode destMode;
    MacAddressMode srcMode;
    uint16_t destShort;
    uint16_t srcShort;
    uint8_t destExtended[8];
    uint8_t srcExtended[8];
};

struct LinkMetricsSample {
    bool valid;
    int8_t rssiDbm;
    uint8_t lqi;
    uint16_t peerShort;
    bool peerShortValid;
    uint64_t capturedAtUs;
    bool fromAck;
};

portMUX_TYPE linkMetricsMux = portMUX_INITIALIZER_UNLOCKED;
LinkMetricsSample latestLinkMetrics = {};

uint16_t readLe16(const uint8_t *data) {
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

bool isUnicastShortAddress(uint16_t address) {
    return address != IEEE802154_BROADCAST_SHORT_ADDR;
}

bool parseMacHeader(const uint8_t *frame, ParsedMacHeader &header) {
    header = {};
    if (frame == nullptr) {
        return false;
    }

    const uint8_t psduLength = frame[0];
    if (psduLength < 5) {
        return false;
    }

    const uint8_t *cursor = frame + 1;
    const uint8_t *const frameEnd = frame + 1 + psduLength;
    if ((cursor + 2) > frameEnd) {
        return false;
    }

    const uint16_t frameControl = readLe16(cursor);
    cursor += 2;

    const bool seqSuppressed = (frameControl & (1U << 8)) != 0;
    if (!seqSuppressed) {
        if ((cursor + 1) > frameEnd) {
            return false;
        }
        cursor += 1;
    }

    header.destMode = static_cast<MacAddressMode>((frameControl >> 10) & 0x3U);
    header.srcMode = static_cast<MacAddressMode>((frameControl >> 14) & 0x3U);
    const bool panIdCompression = (frameControl & (1U << 6)) != 0;

    if (header.destMode != MacAddressMode::None) {
        if ((cursor + 2) > frameEnd) {
            return false;
        }
        cursor += 2;

        if (header.destMode == MacAddressMode::Short) {
            if ((cursor + 2) > frameEnd) {
                return false;
            }
            header.destShort = readLe16(cursor);
            cursor += 2;
        } else if (header.destMode == MacAddressMode::Extended) {
            if ((cursor + 8) > frameEnd) {
                return false;
            }
            memcpy(header.destExtended, cursor, sizeof(header.destExtended));
            cursor += sizeof(header.destExtended);
        } else {
            return false;
        }
    }

    if (header.srcMode != MacAddressMode::None) {
        if (!panIdCompression) {
            if ((cursor + 2) > frameEnd) {
                return false;
            }
            cursor += 2;
        }

        if (header.srcMode == MacAddressMode::Short) {
            if ((cursor + 2) > frameEnd) {
                return false;
            }
            header.srcShort = readLe16(cursor);
            cursor += 2;
        } else if (header.srcMode == MacAddressMode::Extended) {
            if ((cursor + 8) > frameEnd) {
                return false;
            }
            memcpy(header.srcExtended, cursor, sizeof(header.srcExtended));
            cursor += sizeof(header.srcExtended);
        } else {
            return false;
        }
    }

    header.valid = true;
    return true;
}

bool addressMatchesLocalDevice(const ParsedMacHeader &header) {
    if (!header.valid) {
        return false;
    }

    if (header.destMode == MacAddressMode::Short) {
        return header.destShort == esp_ieee802154_get_short_address();
    }

    if (header.destMode == MacAddressMode::Extended) {
        uint8_t localExtended[8] = {};
        if (esp_ieee802154_get_extended_address(localExtended) != ESP_OK) {
            return false;
        }
        return memcmp(header.destExtended, localExtended, sizeof(localExtended)) == 0;
    }

    return false;
}

void storeLinkMetricsSample(int8_t rssiDbm, uint8_t lqi, uint16_t peerShort, bool peerShortValid, bool fromAck) {
    LinkMetricsSample sample = {};
    sample.valid = true;
    sample.rssiDbm = rssiDbm;
    sample.lqi = lqi;
    sample.peerShort = peerShort;
    sample.peerShortValid = peerShortValid;
    sample.capturedAtUs = static_cast<uint64_t>(esp_timer_get_time());
    sample.fromAck = fromAck;

    portENTER_CRITICAL_ISR(&linkMetricsMux);
    latestLinkMetrics = sample;
    portEXIT_CRITICAL_ISR(&linkMetricsMux);
}

bool loadFreshLinkMetricsSample(LinkMetricsSample &sample) {
    portENTER_CRITICAL(&linkMetricsMux);
    sample = latestLinkMetrics;
    portEXIT_CRITICAL(&linkMetricsMux);

    if (!sample.valid) {
        return false;
    }

    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
    if (nowUs < sample.capturedAtUs) {
        return false;
    }

    const uint64_t ageMs = (nowUs - sample.capturedAtUs) / 1000ULL;
    return ageMs <= LINK_METRICS_MAX_AGE_MS;
}

uint8_t deriveLqiFromRssi(int8_t rssiDbm) {
    if (rssiDbm <= LQI_RSSI_FLOOR_DBM) {
        return 0;
    }
    if (rssiDbm >= LQI_RSSI_CEILING_DBM) {
        return 255;
    }

    const int rangeDb = LQI_RSSI_CEILING_DBM - LQI_RSSI_FLOOR_DBM;
    const int offsetDb = rssiDbm - LQI_RSSI_FLOOR_DBM;
    return static_cast<uint8_t>((offsetDb * 255) / rangeDb);
}

bool shouldSendExplicitReports() {
#if MONITOR_ZIGBEE_EXPLICIT_REPORTS
    return true;
#else
    return false;
#endif
}

int8_t clampTxPower(int valueDbm) {
    if (valueDbm < ADAPTIVE_TX_POWER_MIN_DBM) {
        return ADAPTIVE_TX_POWER_MIN_DBM;
    }
    if (valueDbm > ADAPTIVE_TX_POWER_MAX_DBM) {
        return ADAPTIVE_TX_POWER_MAX_DBM;
    }
    return static_cast<int8_t>(valueDbm);
}

void initializeAdaptiveTxPowerState() {
    if (adaptiveTxPowerInitialized) {
        adaptivePreferredTxPowerDbm = clampTxPower(adaptivePreferredTxPowerDbm);
        return;
    }

    adaptivePreferredTxPowerDbm = ADAPTIVE_TX_POWER_MIN_DBM;
    adaptiveSuccessfulJoinCount = 0;
    adaptiveTxPowerInitialized = true;
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

extern "C" void __real_esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *frame_info);
extern "C" void __real_esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack,
                                                    esp_ieee802154_frame_info_t *ack_frame_info);

extern "C" void __wrap_esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *frame_info) {
    if (frame != nullptr && frame_info != nullptr) {
        ParsedMacHeader header = {};
        if (parseMacHeader(frame, header) && addressMatchesLocalDevice(header)) {
            bool peerShortValid = false;
            uint16_t peerShort = 0;
            if (header.srcMode == MacAddressMode::Short && isUnicastShortAddress(header.srcShort)) {
                peerShortValid = true;
                peerShort = header.srcShort;
            }

            if (header.srcMode == MacAddressMode::Short || header.srcMode == MacAddressMode::Extended) {
                storeLinkMetricsSample(frame_info->rssi, frame_info->lqi, peerShort, peerShortValid, false);
            }
        }
    }

    __real_esp_ieee802154_receive_done(frame, frame_info);
}

extern "C" void __wrap_esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack,
                                                    esp_ieee802154_frame_info_t *ack_frame_info) {
    if (frame != nullptr && ack != nullptr && ack_frame_info != nullptr) {
        ParsedMacHeader header = {};
        if (parseMacHeader(frame, header) && header.destMode == MacAddressMode::Short &&
            isUnicastShortAddress(header.destShort)) {
            storeLinkMetricsSample(ack_frame_info->rssi, ack_frame_info->lqi, header.destShort, true, true);
        }
    }

    __real_esp_ieee802154_transmit_done(frame, ack, ack_frame_info);
}

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

        ret = esp_zb_custom_cluster_add_custom_attr(txPowerCluster, CUSTOM_TX_POWER_ATTR_ID, ESP_ZB_ZCL_ATTR_TYPE_S8,
                                                    ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                                                    &txPowerDbm_);
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
        const esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
            getEndpoint(), clusterId, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attrId, value, false);
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
        : tempSensor(sensorEndpoint), pressureSensor(pressureEndpoint), rssiSensor(rssiEndpoint),
          txPowerSensor(txPowerEndpoint) {}

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
    : reportingConfig_(reportingConfig), joinConfig_(joinConfig), lastJoinStatusLogMs_(0),
      lastJoinPowerAdjustmentMs_(0), joinPowerRaisedThisAttempt_(false), reportingConfigured_(false),
      zigbeeStarted_(false) {}

static ZigbeeRuntime &getRuntime(const ZigbeeReportingConfig &reportingConfig) {
    if (!runtimeInitialized) {
        // Placement-new into static storage: avoids heap fragmentation while preserving C++ object
        // construction/destruction semantics for objects that must live for the full boot lifetime.
        new (runtimeStorage) ZigbeeRuntime(reportingConfig.sensorEndpoint, reportingConfig.pressureEndpoint,
                                           reportingConfig.rssiEndpoint, reportingConfig.txPowerEndpoint);
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
        ESP_LOGW(TAG,
                 "Link metric clusters could not be configured; RSSI/LQI/TX-power Zigbee reporting will be disabled");
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
    applyStartupTxPower();
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

    const bool startedFactoryNew = esp_zb_bdb_is_factory_new();
    const uint32_t effectiveJoinTimeoutMs =
        startedFactoryNew ? joinConfig_.zigbeeStartTimeoutMs : joinConfig_.rejoinTimeoutMs;
    ESP_LOGI(TAG, "Waiting for Zigbee network join (factoryNew=%d, timeout=%lu ms)", startedFactoryNew,
             static_cast<unsigned long>(effectiveJoinTimeoutMs));
    const uint32_t joinStartMs = millis();
    lastJoinStatusLogMs_ = joinStartMs;
    lastJoinPowerAdjustmentMs_ = joinStartMs;
    joinPowerRaisedThisAttempt_ = false;

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
        handleJoinWaitTxPower(now);
        if ((now - joinStartMs) >= effectiveJoinTimeoutMs) {
            adaptiveSuccessfulJoinCount = 0;
            ESP_LOGW(TAG, "Zigbee join timed out after %lu ms; continuing with local logging only",
                     static_cast<unsigned long>(effectiveJoinTimeoutMs));
            return false;
        }
        delay(250);
    }

    ESP_LOGI(TAG, "Zigbee connected");
    initializeAdaptiveTxPowerState();
    if (startedFactoryNew) {
        adaptiveSuccessfulJoinCount = 0;
        adaptivePreferredTxPowerDbm = ADAPTIVE_TX_POWER_MIN_DBM;
        ESP_LOGI(TAG,
                 "Initial pairing completed; keeping maximum Zigbee TX power during interview, next joined boot will "
                 "use %d dBm",
                 adaptivePreferredTxPowerDbm);
    } else if (joinPowerRaisedThisAttempt_) {
        adaptiveSuccessfulJoinCount = 0;
    } else if (adaptivePreferredTxPowerDbm > ADAPTIVE_TX_POWER_MIN_DBM) {
        adaptiveSuccessfulJoinCount = static_cast<uint8_t>(adaptiveSuccessfulJoinCount + 1U);
        if (adaptiveSuccessfulJoinCount >= ADAPTIVE_TX_POWER_SUCCESSFUL_JOIN_COUNT) {
            adaptivePreferredTxPowerDbm = clampTxPower(adaptivePreferredTxPowerDbm - ADAPTIVE_TX_POWER_STEP_DBM);
            adaptiveSuccessfulJoinCount = 0;
            ESP_LOGI(TAG, "Repeated successful joins detected; next joined boot will use %d dBm TX power",
                     adaptivePreferredTxPowerDbm);
        } else {
            ESP_LOGI(TAG, "Successful join streak %u/%u at %d dBm TX power", adaptiveSuccessfulJoinCount,
                     ADAPTIVE_TX_POWER_SUCCESSFUL_JOIN_COUNT, adaptivePreferredTxPowerDbm);
        }
    }
    return true;
}

void ZigbeeEnvMonitor::publishMeasurement() {
    ZigbeeRuntime &r = getRuntime(reportingConfig_);
    const float batteryVoltage = readBatteryVoltage(MONITOR_BATTERY_PIN);
    const bool batteryValid = isfinite(batteryVoltage) && batteryVoltage > 0.0f;
    const bool zigbeeConnected = zigbeeStarted_ && Zigbee.connected();
    const bool linkMetricsAvailable = zigbeeStarted_;
    LinkMetricsSample linkMetrics = {};
    const bool freshLinkMetricsAvailable = linkMetricsAvailable && loadFreshLinkMetricsSample(linkMetrics);
    const uint8_t derivedLqi = freshLinkMetricsAvailable ? deriveLqiFromRssi(linkMetrics.rssiDbm) : 0;
    int8_t txPowerDbm = 0;
    if (linkMetricsAvailable) {
        esp_zb_get_tx_power(&txPowerDbm);
    }

    r.bme.measure();
    if (!r.bme.available || !isfinite(r.bme.temperature) || !isfinite(r.bme.humidity) || !isfinite(r.bme.pressure)) {
        ESP_LOGW(TAG, "Invalid measurement sample (bmeAvailable=%d battery=%.2f V zigbeeConnected=%d)", r.bme.available,
                 batteryVoltage, zigbeeConnected);
        return;
    }

    if (r.bme.temperature < TEMPERATURE_MIN_C || r.bme.temperature > TEMPERATURE_MAX_C ||
        r.bme.humidity < HUMIDITY_MIN_PCT || r.bme.humidity > HUMIDITY_MAX_PCT ||
        r.bme.pressure < static_cast<float>(PRESSURE_MIN_HPA) || r.bme.pressure > static_cast<float>(PRESSURE_MAX_HPA)) {
        ESP_LOGW(TAG, "BME280 reading out of physical range (temp=%.2f C, humidity=%.2f%%, pressure=%.1f hPa); discarding",
                 r.bme.temperature, r.bme.humidity, r.bme.pressure);
        return;
    }

    const int16_t pressureHpa = static_cast<int16_t>(lroundf(r.bme.pressure));
    uint8_t batteryPct = 0;
    if (batteryValid) {
        batteryPct = batteryPercentFromVoltage(batteryVoltage);
    }

    if (freshLinkMetricsAvailable) {
        if (linkMetrics.peerShortValid) {
            ESP_LOGI(TAG,
                     "Measurement: temp=%.2f C, humidity=%.2f %%, pressure=%d hPa, battery=%.2f V (%u%%), rssi=%d dBm, "
                     "lqi=%u (raw=%u), txPower=%d dBm, linkPeer=0x%04x, linkSource=%s, zigbeeConnected=%d",
                     r.bme.temperature, r.bme.humidity, pressureHpa, batteryVoltage, batteryPct, linkMetrics.rssiDbm,
                     derivedLqi, linkMetrics.lqi, txPowerDbm, linkMetrics.peerShort, linkMetrics.fromAck ? "ack" : "rx",
                     zigbeeConnected);
        } else {
            ESP_LOGI(TAG,
                     "Measurement: temp=%.2f C, humidity=%.2f %%, pressure=%d hPa, battery=%.2f V (%u%%), rssi=%d dBm, "
                     "lqi=%u (raw=%u), txPower=%d dBm, linkSource=%s, zigbeeConnected=%d",
                     r.bme.temperature, r.bme.humidity, pressureHpa, batteryVoltage, batteryPct, linkMetrics.rssiDbm,
                     derivedLqi, linkMetrics.lqi, txPowerDbm, linkMetrics.fromAck ? "ack" : "rx", zigbeeConnected);
        }
    } else if (linkMetricsAvailable) {
        ESP_LOGI(TAG,
                 "Measurement: temp=%.2f C, humidity=%.2f %%, pressure=%d hPa, battery=%.2f V (%u%%), rssi=n/a, "
                 "lqi=n/a, txPower=%d dBm, zigbeeConnected=%d",
                 r.bme.temperature, r.bme.humidity, pressureHpa, batteryVoltage, batteryPct, txPowerDbm,
                 zigbeeConnected);
    } else {
        ESP_LOGI(TAG,
                 "Measurement: temp=%.2f C, humidity=%.2f %%, pressure=%d hPa, battery=%.2f V (%u%%), rssi=n/a, "
                 "lqi=n/a, txPower=n/a, zigbeeConnected=%d",
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
    if (freshLinkMetricsAvailable) {
        rssiSet = r.tempSensor.setLastRssi(linkMetrics.rssiDbm);
        lqiSet = r.tempSensor.setLastLqi(derivedLqi);
        rssiAnalogSet = r.rssiSensor.setAnalogInput(static_cast<float>(linkMetrics.rssiDbm));
    }
    if (linkMetricsAvailable) {
        txPowerSet = r.tempSensor.setTxPowerDbm(txPowerDbm);
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
        if (freshLinkMetricsAvailable) {
            rssiReported = r.tempSensor.reportLastRssi();
            lqiReported = r.tempSensor.reportLastLqi();
            rssiAnalogReported = r.rssiSensor.reportAnalogInput();
        }
        if (linkMetricsAvailable) {
            txPowerReported = r.tempSensor.reportTxPowerDbm();
            txPowerAnalogReported = r.txPowerSensor.reportAnalogInput();
        }
    }

    ESP_LOGI(TAG,
             "Zigbee attribute update: temp=%.2f C, humidity=%.2f %%, pressure=%d hPa, battery=%.2f V (%u%%), "
             "rssi=%d dBm, lqi=%u (raw=%u), txPower=%d dBm, explicitReports=%d "
             "(setT=%d setH=%d setP=%d setRSSI=%d setLQI=%d setTxPower=%d setRSSIAnalog=%d setTxPowerAnalog=%d "
             "setBattPct=%d setBattV=%d reportTH=%d reportP=%d reportRSSI=%d reportLQI=%d reportTxPower=%d "
             "reportRSSIAnalog=%d reportTxPowerAnalog=%d reportBatt=%d)",
             r.bme.temperature, r.bme.humidity, pressureHpa, batteryVoltage, batteryPct,
             freshLinkMetricsAvailable ? linkMetrics.rssiDbm : 0, freshLinkMetricsAvailable ? derivedLqi : 0,
             freshLinkMetricsAvailable ? linkMetrics.lqi : 0, txPowerDbm, shouldSendExplicitReports(), tempSet, humSet,
             pressureSet, rssiSet, lqiSet, txPowerSet, rssiAnalogSet, txPowerAnalogSet, batteryPctSet, batteryVoltSet,
             tempHumidityReported, pressureReported, rssiReported, lqiReported, txPowerReported, rssiAnalogReported,
             txPowerAnalogReported, batteryReported);
}

void ZigbeeEnvMonitor::factoryResetNetwork() {
    if (!zigbeeStarted_) {
        ESP_LOGW(TAG, "Zigbee stack not started; skipping network factory reset");
        return;
    }

    initializeAdaptiveTxPowerState();
    adaptivePreferredTxPowerDbm = ADAPTIVE_TX_POWER_MIN_DBM;
    adaptiveSuccessfulJoinCount = 0;
    ESP_LOGW(TAG, "Resetting Zigbee network state and restarting join");
    Zigbee.factoryReset();
}

bool ZigbeeEnvMonitor::isStarted() const {
    return zigbeeStarted_;
}

void ZigbeeEnvMonitor::resetAdaptiveTxPowerRtcState() {
    adaptiveTxPowerInitialized = false;
    adaptivePreferredTxPowerDbm = ADAPTIVE_TX_POWER_MIN_DBM;
    adaptiveSuccessfulJoinCount = 0;
}

void ZigbeeEnvMonitor::applyStartupTxPower() {
    initializeAdaptiveTxPowerState();

    if (!esp_zb_bdb_is_factory_new()) {
        setTxPowerDbm(adaptivePreferredTxPowerDbm, "Using adaptive Zigbee TX power for joined network");
        return;
    }

    setTxPowerDbm(ADAPTIVE_TX_POWER_MAX_DBM, "Using maximum Zigbee TX power for pairing");
}

void ZigbeeEnvMonitor::handleJoinWaitTxPower(uint32_t nowMs) {
    initializeAdaptiveTxPowerState();

    if ((nowMs - lastJoinPowerAdjustmentMs_) < joinConfig_.joinStatusLogIntervalMs) {
        return;
    }

    int8_t currentTxPowerDbm = ADAPTIVE_TX_POWER_MIN_DBM;
    esp_zb_get_tx_power(&currentTxPowerDbm);
    const int8_t increasedTxPowerDbm = clampTxPower(currentTxPowerDbm + ADAPTIVE_TX_POWER_STEP_DBM);
    if (increasedTxPowerDbm == currentTxPowerDbm) {
        lastJoinPowerAdjustmentMs_ = nowMs;
        return;
    }

    adaptivePreferredTxPowerDbm = increasedTxPowerDbm;
    joinPowerRaisedThisAttempt_ = true;
    adaptiveSuccessfulJoinCount = 0;
    setTxPowerDbm(increasedTxPowerDbm, "Join still pending; increasing Zigbee TX power");
    lastJoinPowerAdjustmentMs_ = nowMs;
}

bool ZigbeeEnvMonitor::setTxPowerDbm(int8_t txPowerDbm, const char *reason) {
    const int8_t requestedTxPowerDbm = clampTxPower(txPowerDbm);
    int8_t currentTxPowerDbm = ADAPTIVE_TX_POWER_MIN_DBM;
    esp_zb_get_tx_power(&currentTxPowerDbm);
    if (currentTxPowerDbm == requestedTxPowerDbm) {
        return true;
    }

    esp_zb_set_tx_power(requestedTxPowerDbm);
    int8_t appliedTxPowerDbm = requestedTxPowerDbm;
    esp_zb_get_tx_power(&appliedTxPowerDbm);
    adaptivePreferredTxPowerDbm = clampTxPower(appliedTxPowerDbm);
    ESP_LOGI(TAG, "%s: %d dBm", reason, appliedTxPowerDbm);
    return appliedTxPowerDbm == requestedTxPowerDbm;
}
