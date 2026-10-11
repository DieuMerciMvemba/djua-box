#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <Arduino.h>

#include "../sensors/power_sensor.h"
#include "../sensors/solar_power_sensor.h"
#include "../sensors/dc_load_power_sensor.h"
#include "../sensors/ac_power_sensor.h"
#include "../sensors/gps.h"
#include "../sensors/rtc_ds1302.h"
#include "telemetry_v3.h"

// =====================================================
// IOT BOX - STRUCTURE DE TELEMETRIE
// =====================================================

struct TelemetryData
{
    // HORODATAGE REEL DS1302 AU FORMAT ISO 8601
    char timestamp[32];
    bool timestampValid;

    // GPS REEL
    double latitude;
    double longitude;

    // BATTERIE REELLE - INA219
    float batteryVoltage;
    float batteryCurrent;
    float batteryPower;
    bool batteryMeasurementValid;

    // PANNEAU SOLAIRE - INA219 INDEPENDANT
    float solarVoltage;
    float solarCurrent;
    float solarPower;
    float solarEnergyIntervalWh;
    bool solarMeasurementValid;

    // CHARGE AC - ZMPT101B + ACS712-30A
    float acLoadVoltage;
    float acLoadCurrent;
    float acActivePower;
    float acApparentPower;
    float acPowerFactor;
    float acEnergyIntervalWh;
    float acEnergyIntervalVAh;
    bool acMeasurementValid;

    // CHARGES DC - TROISIEME INA219, SHUNT EXTERNE
    float dcLoadVoltage, dcLoadCurrent, dcLoadPower, dcLoadEnergyIntervalWh;
    bool dcLoadMeasurementValid, dcLoadEnergyComplete;

    // Intervalle entre deux transmissions
    unsigned long intervalSeconds;
    bool v3Present = false;
    djua::TelemetryV3 v3;
};


// Construit une telemetrie complete
TelemetryData buildTelemetry(
    const PowerData& battery,
    const SolarPowerData& solar,
    const ACPowerData& acLoad,
    const GPSData& gps,
    const RTCData& rtc,
    unsigned long intervalSeconds,
    const DcLoadPowerData& dcLoad = DcLoadPowerData{}
);
TelemetryData buildV3Telemetry(const djua::TelemetryV3& frame, const RTCData& rtc);

#endif
