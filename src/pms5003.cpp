#include "pms5003.h"
#include "BoardConfig.h"
#include <Adafruit_PM25AQI.h>
#include <ArduinoLog.h>
#include <math.h>

Pms5003Reading readPms5003() {
    Pms5003Reading reading{false, NAN, NAN, NAN};

#if MONITOR_PMS5003_ENABLED
    static bool initialized = false;
    static HardwareSerial pmsSerial(MONITOR_PMS5003_UART_NUM);
    static Adafruit_PM25AQI aqi;

    if (!initialized) {
        pmsSerial.begin(MONITOR_PMS5003_BAUD, SERIAL_8N1, MONITOR_PMS5003_RX_PIN, MONITOR_PMS5003_TX_PIN);
        if (!aqi.begin_UART(&pmsSerial)) {
            Log.warning("PMS5003 not detected on UART%d (RX=%d, TX=%d)" CR, MONITOR_PMS5003_UART_NUM,
                        MONITOR_PMS5003_RX_PIN, MONITOR_PMS5003_TX_PIN);
            return reading;
        }
        initialized = true;
        Log.notice("PMS5003 initialized on UART%d" CR, MONITOR_PMS5003_UART_NUM);
    }

    PM25_AQI_Data data;
    if (!aqi.read(&data)) {
        Log.warning("Failed to read PMS5003 sample" CR);
        return reading;
    }

    reading.valid = true;
    // Library exposes PM1.0/2.5/10 as pm10_standard, pm25_standard, pm100_standard.
    reading.pm1_0 = data.pm10_standard;
    reading.pm2_5 = data.pm25_standard;
    reading.pm10 = data.pm100_standard;
#endif

    return reading;
}
