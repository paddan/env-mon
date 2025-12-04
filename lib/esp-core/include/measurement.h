#ifndef _MEASUREMENT_H_
#define _MEASUREMENT_H_

typedef struct Data {
    float temperature;
    float humidity;
    float pressure;
    float batteryVoltage;
    float pm1_0;
    float pm2_5;
    float pm10;
    bool hasPm;
} Data;

#endif
