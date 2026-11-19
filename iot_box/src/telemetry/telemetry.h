#pragma once
#include "telemetry_v3.h"
#include "../communication/d2_codec.h"

struct RTCData;

// Une seule representation des mesures : INA228, INA3221 et PZEM.
struct TelemetryData {
    char timestamp[32] = {};
    bool timestampValid = false;
    djua::TelemetryV3 v3;
};

TelemetryData buildV3Telemetry(const djua::TelemetryV3& frame, const RTCData& rtc);

inline djua::D2Frame buildD2TelemetryFrame(const djua::TelemetryV3& source) {
    djua::D2Frame frame;
    frame.rtc = source.rtcValid ? source.rtc : 0;
    frame.uptime = source.uptime;
    frame.interval = source.interval;
    if (source.rtcValid) frame.flags |= 1U;
    if (source.location.valid) {
        frame.flags |= 1U << 1;
        frame.values[djua::Lat] = {source.location.latitude, true};
        frame.values[djua::Lon] = {source.location.longitude, true};
    }
    if (source.battery.valid) {
        frame.flags |= 1U << 2;
        frame.values[djua::BatteryV] = {source.battery.voltage, true};
        frame.values[djua::BatteryI] = {source.battery.current, true};
        frame.values[djua::BatteryP] = {source.battery.power, true};
    }
    if (source.solar.valid) {
        frame.flags |= 1U << 3;
        frame.values[djua::SolarV] = {source.solar.voltage, true};
        frame.values[djua::SolarI] = {source.solar.current, true};
        frame.values[djua::SolarP] = {source.solar.power, true};
        if (source.solar.energyComplete) {
            frame.flags |= 1U << 5;
            frame.values[djua::SolarE] = {source.solar.energyWh, true};
        }
    }
    if (source.ac.electric.valid) {
        frame.flags |= 1U << 4;
        frame.values[djua::AcV] = {source.ac.electric.voltage, true};
        frame.values[djua::AcI] = {source.ac.electric.current, true};
        frame.values[djua::AcP] = {source.ac.electric.power, true};
        frame.values[djua::AcS] = {source.ac.apparentVA, true};
        if (source.ac.electric.energyComplete) {
            frame.flags |= 1U << 6;
            frame.values[djua::AcE] = {source.ac.electric.energyWh, true};
        }
    }
    const auto dc = djua::aggregateDcLoad(source.dc);
    frame.dcMeasuredCount = dc.measuredCount;
    if (dc.electric.valid) {
        frame.flags |= 1U << 7;
        frame.values[djua::DcV] = {dc.electric.voltage, true};
        frame.values[djua::DcI] = {dc.electric.current, true};
        frame.values[djua::DcP] = {dc.electric.power, true};
        if (dc.electric.energyComplete) {
            frame.flags |= 1U << 8;
            frame.values[djua::DcE] = {dc.electric.energyWh, true};
        }
    }
    return frame;
}
