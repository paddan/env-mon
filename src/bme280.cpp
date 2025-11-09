#include "bme280.h"
#include "BoardConfig.h"
#include <ArduinoLog.h>

Adafruit_BME280 bme;

Bme280Sensor::Bme280Sensor() {
    Wire.begin(MONITOR_I2C_SDA_PIN, MONITOR_I2C_SCL_PIN);

    if (!bme.begin(0x76, &Wire)) {
        Log.error("Could not find a valid BME280 sensor, check wiring!" CR);
        while (true)
            ;
    }

    bme.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X2, // temperature
                    Adafruit_BME280::SAMPLING_X1,                               // pressure
                    Adafruit_BME280::SAMPLING_X4,                               // humidity
                    Adafruit_BME280::FILTER_OFF);
}

void Bme280Sensor::measure() {
    bme.takeForcedMeasurement();
    temperature = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure = bme.readPressure() / 100.0F;
}
