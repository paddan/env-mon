#include "bme280.h"
#include "BoardConfig.h"
#include <esp_log.h>
#include <math.h>

namespace {
constexpr char TAG[] = "bme280";
constexpr uint8_t BME280_ADDR_LOW = 0x76;
constexpr uint8_t BME280_ADDR_HIGH = 0x77;

void scanI2CBus(TwoWire &wire) {
    bool foundAny = false;
    for (uint8_t address = 0x03; address <= 0x77; ++address) {
        wire.beginTransmission(address);
        const uint8_t err = wire.endTransmission();
        if (err == 0) {
            foundAny = true;
            ESP_LOGI(TAG, "I2C device found at 0x%02X", address);
        }
    }

    if (!foundAny) {
        ESP_LOGW(TAG, "No I2C devices found on SDA=%d, SCL=%d", MONITOR_BME280_SDA_PIN, MONITOR_BME280_SCL_PIN);
    }
}
bool tryBegin(Adafruit_BME280 &sensor, TwoWire &wire, uint8_t address) {
    return sensor.begin(address, &wire);
}
} // namespace

Bme280Sensor::Bme280Sensor() : temperature(NAN), humidity(NAN), pressure(NAN), available(false) {

    Wire.begin(MONITOR_BME280_SDA_PIN, MONITOR_BME280_SCL_PIN);
    Wire.setClock(100000);

    ESP_LOGI(TAG, "Initializing BME280 on SDA=%d, SCL=%d", MONITOR_BME280_SDA_PIN, MONITOR_BME280_SCL_PIN);

#if MONITOR_BME280_SCAN_AT_BOOT
    scanI2CBus(Wire);
#endif

    uint8_t detectedAddress = 0;
    if (tryBegin(sensor_, Wire, BME280_ADDR_LOW)) {
        detectedAddress = BME280_ADDR_LOW;
    } else if (tryBegin(sensor_, Wire, BME280_ADDR_HIGH)) {
        detectedAddress = BME280_ADDR_HIGH;
    } else {
        ESP_LOGE(TAG, "Could not find BME280 at 0x76 or 0x77 (SDA=%d, SCL=%d)", MONITOR_BME280_SDA_PIN,
                 MONITOR_BME280_SCL_PIN);
        ESP_LOGE(TAG, "Verify wiring and configured pins MONITOR_BME280_SDA_PIN/MONITOR_BME280_SCL_PIN");

#if !MONITOR_BME280_SCAN_AT_BOOT
        scanI2CBus(Wire);
#endif
        return;
    }
    ESP_LOGI(TAG, "BME280 initialized at I2C address 0x%02X", detectedAddress);

    sensor_.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X2, // temperature
                        Adafruit_BME280::SAMPLING_X1,                               // pressure
                        Adafruit_BME280::SAMPLING_X4,                               // humidity
                        Adafruit_BME280::FILTER_OFF);
    available = true;
}

void Bme280Sensor::measure() {
    if (!available) {
        temperature = NAN;
        humidity = NAN;
        pressure = NAN;
        return;
    }

    sensor_.takeForcedMeasurement();
    temperature = sensor_.readTemperature();
    humidity = sensor_.readHumidity();
    pressure = sensor_.readPressure() / 100.0F;
}
