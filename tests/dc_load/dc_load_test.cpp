#include <cassert>
#include <cmath>
#include <iostream>
#include "Wire.h"
#include "../../src/sensors/dc_load_power_sensor.cpp"
#include "../../src/telemetry/telemetry.cpp"
#include "../../src/indicators/indicators.cpp"
#include "../../src/status/device_status.cpp"
#include "../../src/telemetry/energy_window.h"
bool formatRTCTimestamp(const RTCData&, char* output, size_t) { output[0] = 0; return false; }
#if DC_LOAD_MEASUREMENT_ENABLED && DC_LOAD_SHUNT_CONFIGURED
static void resetDc() {
    dcPresent = dcRegisterOk = dcCalibrationOk = true;
    dcDefault = DcTestSample{}; dcSamples.clear(); dcSampleIndex = 0;
}
#endif
int main() {
    DcLoadCalibration c;
    assert(makeDcLoadCalibration(50, 75, c));
    assert(std::fabs(c.shuntOhms - 0.0015) < 1e-12 && c.reg == 16384);
    assert(std::fabs(c.currentLsbA - 1.0 / 600) < 1e-12);
    assert(makeDcLoadCalibration(100, 1, c) && c.reg > 0 && !(c.reg & 1));
    assert(!makeDcLoadCalibration(0, 75, c) && !makeDcLoadCalibration(50, 320, c));
    assert(!makeDcLoadCalibration(NAN, 75, c));
    initIndicators(); initDeviceStatus(true, true);
#if DC_LOAD_MEASUREMENT_ENABLED && DC_LOAD_SHUNT_CONFIGURED
    resetDc(); assert(initDcLoadPowerSensor());
    auto dc = readDcLoadPowerSensor();
    assert(dc.valid && dc.sampleCount == 7 && !dc.recoveryAttempted);
    assert(std::fabs(dc.voltage - 12) < 1e-4 && std::fabs(dc.current - 10 * DC_LOAD_CURRENT_POLARITY) < 1e-4);
    // Power uses load-side VIN-, not upstream voltage plus shunt loss.
    assert(std::fabs(dc.power - 120 * DC_LOAD_CURRENT_POLARITY) < 0.001);
    auto telemetry = buildTelemetry(PowerData{}, SolarPowerData{}, ACPowerData{}, GPSData{}, RTCData{}, 10, dc);
    assert(telemetry.dcLoadMeasurementValid && std::fabs(telemetry.dcLoadVoltage - 12) < 1e-4);
    assert(!telemetry.dcLoadEnergyComplete && std::isnan(telemetry.dcLoadEnergyIntervalWh));
    resetDc(); dcDefault.current = dcDefault.shunt = 0;
    dc = readDcLoadPowerSensor(); assert(dc.valid && dc.power == 0);
    dcDefault.current = -6000; dcDefault.shunt = -1500;
    dc = readDcLoadPowerSensor(); assert(dc.valid && dc.current * DC_LOAD_CURRENT_POLARITY < 0);
    // Median removes one outlier; minority invalid samples remain rejectable.
    resetDc(); dcSamples.assign(7, dcDefault); dcSamples[0].current = 12000;
    dcSamples[1].bus |= 1; dcSamples[2].bus = 0; // not ready.
    dc = readDcLoadPowerSensor(); assert(dc.valid && dc.sampleCount == 5);
    assert(std::fabs(dc.current - 10 * DC_LOAD_CURRENT_POLARITY) < 1e-4);
    for (int fault = 0; fault < 7; ++fault) {
        resetDc();
        if (fault == 0) dcDefault.bus |= 1; // overflow
        if (fault == 1) dcDefault.bus = (7000U << 3) | 2U; // 28 V
        if (fault == 2) dcDefault.shunt = 8000; // shunt above rating
        if (fault == 3) dcDefault.current = 32767; // current saturation
        if (fault == 4) dcCalibrationOk = false;
        if (fault == 5) dcRegisterOk = false;
        if (fault == 6) dcDefault.bus &= ~2U;
        dc = readDcLoadPowerSensor(); assert(!dc.valid && dc.recoveryAttempted);
        resetDc(); assert(initDcLoadPowerSensor());
    }
    dcPresent = false;
    for (int n = 0; n < 3; ++n) {
        dc = readDcLoadPowerSensor(); assert(!dc.valid && dc.recoveryAttempted);
        recordDcLoadStatus(isDcLoadPowerSensorAvailable(), dc.valid);
    }
    assert(getAlertIndicatorState() == ALERT_WARNING);
    resetDc(); dc = readDcLoadPowerSensor(); assert(dc.valid && dc.recoveryAttempted);
    recordDcLoadStatus(true, true); assert(getAlertIndicatorState() == ALERT_NONE);
    recordGeofenceStatus(true, false);
    for (int n = 0; n < 3; ++n) recordDcLoadStatus(false, false);
    assert(getAlertIndicatorState() == ALERT_GEOFENCE);
#else
    assert(!initDcLoadPowerSensor() && !isDcLoadPowerSensorAvailable());
    for (int n = 0; n < 5; ++n) {
        auto dc = readDcLoadPowerSensor(); assert(!dc.valid && !dc.recoveryAttempted);
        recordDcLoadStatus(false, false);
    }
    assert(dcWrites == 0 && getAlertIndicatorState() == ALERT_NONE);
#endif
    assert(dcOtherAddresses == 0);
    djua::EnergyWindow energy;
    energy.sample(0, 120, true); energy.sample(10000, 240, true);
    assert(energy.complete(10000) && std::fabs(energy.wattHours() - 0.5) < 1e-9);
    energy.sample(20000, 0, false); energy.sample(30000, 240, true);
    assert(!energy.complete(30000));
    energy.reset(30000, -360, true); energy.sample(40000, -360, true);
    assert(energy.complete(40000) && std::fabs(energy.wattHours() + 1) < 1e-9);
    energy.sample(60001, -360, true); assert(!energy.complete(60001));
    std::cout << "PASS: external shunt calibration, DC driver, polarity, faults/recovery, telemetry, alerts, energy\n";
}
