#ifndef _BME280_H_
#define _BME280_H_

#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

class Bme280Sensor {
  public:
    float temperature;
    float humidity;
    float pressure;
    bool available;

    Bme280Sensor();
    void measure();
};
#endif
