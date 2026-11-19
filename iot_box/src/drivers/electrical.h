#pragma once
#include <stdint.h>
#include <math.h>

namespace djua {
enum class MeasureFault : uint8_t {
    None, Disabled, Configuration, AddressConflict, Io, WrongDevice,
    NotReady, Overflow, Range, Timeout, Crc, Protocol, CounterReset
};
struct ElectricalReading {
    double voltage = 0, current = 0, power = 0, shuntV = 0;
    uint32_t sampledAt = 0;
    bool valid = false;
    MeasureFault fault = MeasureFault::Disabled;
};
class ElectricalSensor {
public:
    virtual ~ElectricalSensor() = default;
    virtual bool begin() = 0;
    virtual bool start(uint32_t now) = 0;
    virtual ElectricalReading read(uint32_t now) = 0;
};
struct ShuntConfig {
    double ohms, maxCurrentA, maxBusV, ratedWatts;
    int polarity = 1;
};
inline bool validShunt(const ShuntConfig& c, double maxBus, double shuntRange) {
    return isfinite(c.ohms) && isfinite(c.maxCurrentA) && isfinite(c.maxBusV) &&
        isfinite(c.ratedWatts) && c.ohms > 0 && c.maxCurrentA > 0 &&
        c.maxBusV > 0 && c.maxBusV <= maxBus && c.ratedWatts > 0 &&
        c.maxCurrentA * c.ohms < shuntRange &&
        c.maxCurrentA * c.maxCurrentA * c.ohms <= c.ratedWatts &&
        (c.polarity == 1 || c.polarity == -1);
}
}
