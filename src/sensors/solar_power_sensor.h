#ifndef SOLAR_POWER_SENSOR_H
#define SOLAR_POWER_SENSOR_H

#include <Arduino.h>

struct SolarPowerData
{
    float voltage;
    float current;
    float power;
    float busVoltage;
    float shuntVoltageMv;
    float sensorCurrentMa;
    uint8_t sampleCount;
    bool recoveryAttempted;
    bool valid;
};

// Le bus Wire doit deja etre initialise (par le driver batterie en production).
bool initSolarPowerSensor();
SolarPowerData readSolarPowerSensor();
bool isSolarPowerSensorAvailable();

#endif
