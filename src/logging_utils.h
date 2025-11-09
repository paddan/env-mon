#pragma once

#include <Arduino.h>
#include "mqtt.h"

// Initializes ArduinoLog once per boot and returns true when a USB serial session is active.
bool initLogging();

// Allows other modules to gate verbose output on the current USB/serial state.
bool isLoggingEnabled();

// Emits a timestamped measurement line when logging is enabled (usually only while USB is attached).
void logLatestMeasurement(const Data &measurement);
