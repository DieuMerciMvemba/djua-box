#include "../../src/telemetry/json_v3.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
    TelemetryData data{}; data.v3Present = true; data.v3.interval = 1800;
    data.v3.location.source = djua::LocationSource::NeoGnss;
    data.v3.battery = {12, -1, -12, -6, true, true, djua::MeasureFault::None, 10};
    data.v3.dc[0] = {12, 0, 0, 0, true, true, djua::MeasureFault::None, 10};
    data.v3.dc[1].fault = djua::MeasureFault::Io;
    data.v3.ac.electric = {230, 1, 200, 0, true, false, djua::MeasureFault::None, 100};
    data.v3.ac.apparentVA = 230; data.v3.ac.frequencyHz = 50; data.v3.ac.powerFactor = .87;
    data.v3.ac.energyFault = djua::MeasureFault::CounterReset;
    JsonDocument doc; telemetryJsonV3(doc, data);
    assert(!doc.overflowed() && doc["latitude"].isNull() && doc["location"]["accuracy_m"].isNull());
    assert(doc["battery"]["current_a"].as<double>() == -1 && doc["battery"]["energy_interval_wh"].as<double>() == -6);
    assert(doc["dc_ports"].size() == 4);
    assert(!strcmp(doc["dc_ports"][3]["id"].as<const char*>(), "TV"));
    assert(!doc["dc_ports"][0]["power_w"].isNull() && doc["dc_ports"][0]["power_w"].as<double>() == 0);
    assert(doc["dc_ports"][1]["power_w"].isNull() && doc["ac_load"]["energy_interval_wh"].isNull());
    assert(doc["ac_load"]["frequency_hz"].as<double>() == 50);
    char text[3072]; size_t size = serializeJson(doc, text, sizeof(text)); assert(size > 0 && size < sizeof(text));
    puts("PASS real ArduinoJson V3: four identities, null versus zero, signed battery, incomplete AC counter energy, location accuracy unknown");
}
