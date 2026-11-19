#pragma once
#include <ArduinoJson.h>
#include "telemetry.h"
#include "../config.h"
inline void electricalJsonV3(JsonObject object, const djua::ElectricalSlot& value) {
    object["valid"] = value.valid; object["fault"] = static_cast<unsigned>(value.fault);
    object["age_ms"] = value.ageMs; object["energy_complete"] = value.energyComplete;
    if (value.valid) {
        object["voltage_v"] = value.voltage; object["current_a"] = value.current; object["power_w"] = value.power;
    } else { object["voltage_v"] = nullptr; object["current_a"] = nullptr; object["power_w"] = nullptr; }
    if (value.energyComplete) object["energy_interval_wh"] = value.energyWh;
    else object["energy_interval_wh"] = nullptr;
}
inline void telemetryJsonV3(JsonDocument& doc, const TelemetryData& data) {
    doc.clear(); const auto& frame = data.v3;
    doc["schema_version"] = 3; doc["kit_id"] = DEVICE_ID;
    doc["interval_seconds"] = frame.interval; doc["uptime_ms"] = frame.uptime;
    if (data.timestampValid) { doc["timestamp"] = data.timestamp; doc["timezone"] = RTC_TIMEZONE_LABEL; }
    auto location = doc["location"].to<JsonObject>();
    location["source"] = static_cast<unsigned>(frame.location.source);
    location["valid"] = frame.location.valid; location["age_ms"] = frame.location.ageMs;
    if (frame.location.valid) {
        doc["latitude"] = frame.location.latitude; doc["longitude"] = frame.location.longitude;
    } else { doc["latitude"] = nullptr; doc["longitude"] = nullptr; }
    if (frame.location.accuracyKnown) location["accuracy_m"] = frame.location.accuracyM;
    else location["accuracy_m"] = nullptr;
    if (frame.location.hdopKnown) location["hdop"] = frame.location.hdop;
    else location["hdop"] = nullptr;
    electricalJsonV3(doc["battery"].to<JsonObject>(), frame.battery);
    electricalJsonV3(doc["solar"].to<JsonObject>(), frame.solar);
    auto ac = doc["ac_load"].to<JsonObject>(); electricalJsonV3(ac, frame.ac.electric);
    ac["energy_fault"] = static_cast<unsigned>(frame.ac.energyFault);
    if (frame.ac.electric.valid) {
        ac["active_power_w"] = frame.ac.electric.power; ac["apparent_power_va"] = frame.ac.apparentVA;
        ac["frequency_hz"] = frame.ac.frequencyHz; ac["power_factor"] = frame.ac.powerFactor;
        ac["alarm"] = frame.ac.alarm;
    } else {
        ac["active_power_w"] = nullptr; ac["apparent_power_va"] = nullptr;
        ac["frequency_hz"] = nullptr; ac["power_factor"] = nullptr; ac["alarm"] = nullptr;
    }
    if (frame.ac.counterValid) ac["energy_counter_wh"] = frame.ac.counterWh;
    else ac["energy_counter_wh"] = nullptr;
    auto ports = doc["dc_ports"].to<JsonArray>();
    static const char* ids[] = {"DC1", "DC2", "DC3", "TV"};
    for (unsigned i = 0; i < 4; ++i) {
        auto port = ports.add<JsonObject>(); port["id"] = ids[i]; electricalJsonV3(port, frame.dc[i]);
    }
}
