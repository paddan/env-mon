#include "bme280.h"
#include "BoardConfig.h"
#include <ArduinoLog.h>
#include <math.h>

Adafruit_BME280 bme;

Bme280Sensor::Bme280Sensor() {
    available = false;
    temperature = NAN;
    humidity = NAN;
    pressure = NAN;

    Wire.begin(MONITOR_BME280_SDA_PIN, MONITOR_BME280_SCL_PIN);

    if (!bme.begin(0x76, &Wire)) {
        Log.error("Could not find a valid BME280 sensor, check wiring!" CR);
        return;
    }

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
