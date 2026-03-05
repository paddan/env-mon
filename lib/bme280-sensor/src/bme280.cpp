#include "bme280.h"
#include "BoardConfig.h"
#include <esp_log.h>
#include <math.h>

Adafruit_BME280 bme;

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
} // namespace

Bme280Sensor::Bme280Sensor() {
    available = false;
    temperature = NAN;
    humidity = NAN;
    pressure = NAN;

    Wire.begin(MONITOR_BME280_SDA_PIN, MONITOR_BME280_SCL_PIN);
    Wire.setClock(100000);

    ESP_LOGI(TAG, "Initializing BME280 on SDA=%d, SCL=%d", MONITOR_BME280_SDA_PIN, MONITOR_BME280_SCL_PIN);
    scanI2CBus(Wire);

    uint8_t detectedAddress = 0;
    if (bme.begin(BME280_ADDR_LOW, &Wire)) {
        detectedAddress = BME280_ADDR_LOW;
    } else if (bme.begin(BME280_ADDR_HIGH, &Wire)) {
        detectedAddress = BME280_ADDR_HIGH;
    } else {
        ESP_LOGE(TAG, "Could not find BME280 at 0x76 or 0x77 (SDA=%d, SCL=%d)", MONITOR_BME280_SDA_PIN,
                 MONITOR_BME280_SCL_PIN);
        ESP_LOGE(TAG, "Verify wiring and configured pins MONITOR_BME280_SDA_PIN/MONITOR_BME280_SCL_PIN");
        return;
    }
    ESP_LOGI(TAG, "BME280 initialized at I2C address 0x%02X", detectedAddress);

    bme.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X2, // temperature
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

    bme.takeForcedMeasurement();
    temperature = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure = bme.readPressure() / 100.0F;
}
