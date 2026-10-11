#pragma once
#include <Arduino.h>
struct DcLoadPowerData {
    float voltage = 0, current = 0, power = 0;
    float busVoltage = 0, shuntVoltageMv = 0, sensorCurrentMa = 0;
    uint8_t sampleCount = 0;
    bool recoveryAttempted = false, valid = false;
};
// Wire deja initialise par le capteur batterie. Pas de reinitialisation du bus.
bool initDcLoadPowerSensor();
DcLoadPowerData readDcLoadPowerSensor();
bool isDcLoadPowerSensorAvailable();
