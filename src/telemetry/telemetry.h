#pragma once
#include "telemetry_v3.h"
#include "../sensors/rtc_ds1302.h"

// Une seule representation des mesures : INA228, INA3221 et PZEM.
struct TelemetryData {
    char timestamp[32] = {};
    bool timestampValid = false;
    djua::TelemetryV3 v3;
};

TelemetryData buildV3Telemetry(const djua::TelemetryV3& frame, const RTCData& rtc);
