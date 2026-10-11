#pragma once
#include "../drivers/electrical.h"
#include "../drivers/pzem004t.h"
#include "../location/location.h"
namespace djua {
enum class DcPort : uint8_t { Dc1, Dc2, Dc3, Tv };
struct ElectricalSlot {
    double voltage = 0, current = 0, power = 0, energyWh = 0;
    bool valid = false, energyComplete = false;
    MeasureFault fault = MeasureFault::Disabled;
    uint16_t ageMs = 0; // 65535 means >=65535 ms
};
struct AcSlot {
    ElectricalSlot electric;
    double apparentVA = 0, frequencyHz = 0, powerFactor = 0;
    uint32_t counterWh = 0;
    bool counterValid = false, alarm = false;
    MeasureFault energyFault = MeasureFault::Disabled;
};
struct TelemetryV3 {
    char device[33] = {};
    uint32_t sequence = 0, snapshot = 0, rtc = 0, uptime = 0, interval = 0;
    uint8_t part = 0;
    bool rtcValid = false;
    LocationFix location;
    ElectricalSlot battery, solar;
    AcSlot ac;
    ElectricalSlot dc[4]; // fixed identities DC1, DC2, DC3, TV, never USB
};
inline uint16_t boundedAge(uint32_t age) { return static_cast<uint16_t>(age > 65535 ? 65535 : age); }
inline ElectricalSlot makeSlot(const ElectricalReading& r, uint32_t now, double wh, bool complete) {
    ElectricalSlot slot; slot.fault = r.fault; slot.ageMs = boundedAge(now - r.sampledAt);
    slot.valid = r.valid; slot.energyComplete = r.valid && complete;
    if (r.valid) { slot.voltage = r.voltage; slot.current = r.current; slot.power = r.power; }
    if (slot.energyComplete) slot.energyWh = wh;
    return slot;
}
}
