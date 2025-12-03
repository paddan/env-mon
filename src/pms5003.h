#pragma once

#include <Arduino.h>

struct Pms5003Reading {
    bool valid;
    float pm1_0;
    float pm2_5;
    float pm10;
};

// Attempts to read a sample from the PMS5003 UART sensor. The call is a no-op
// when MONITOR_PMS5003_ENABLED is false. On success, returns a struct with
// populated particulate values in µg/m³ and valid=true.
Pms5003Reading readPms5003();
