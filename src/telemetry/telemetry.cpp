#include "telemetry.h"
#include "../sensors/rtc_ds1302.h"

TelemetryData buildV3Telemetry(const djua::TelemetryV3& frame, const RTCData& rtc) {
    TelemetryData data{};
    data.v3 = frame;
    data.timestampValid = formatRTCTimestamp(rtc, data.timestamp, sizeof(data.timestamp));
    return data;
}
