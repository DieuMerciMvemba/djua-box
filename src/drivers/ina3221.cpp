#include "ina3221.h"
namespace djua {
bool Ina3221::begin() {
    ready_ = pending_ = false; fault_ = MeasureFault::Configuration;
    if (address_ < 0x40 || address_ > 0x43) return false;
    for (const auto& c : channels_) if (!validShunt(c, 26, 0.1638)) return false;
    if (!bus_.claim(address_, this)) { fault_ = MeasureFault::AddressConflict; return false; }
    uint32_t manufacturer = 0, die = 0, check = 0;
    fault_ = MeasureFault::Io;
    if (!bus_.read(address_, 0xfe, manufacturer) || !bus_.read(address_, 0xff, die)) return false;
    if (manufacturer != 0x5449 || die != 0x3220) { fault_ = MeasureFault::WrongDevice; return false; }
    if (!bus_.write(address_, 0, 0x7120) || !bus_.read(address_, 0, check) || check != 0x7120) return false;
    ready_ = true; fault_ = MeasureFault::None; return true;
}
bool Ina3221::start(uint32_t now) {
    if (!ready_ && !begin()) return false;
    pending_ = bus_.write(address_, 0, 0x7123); // all three channels, single shot
    startedAt_ = now; fault_ = pending_ ? MeasureFault::NotReady : MeasureFault::Io;
    if (!pending_) ready_ = false;
    return pending_;
}
bool Ina3221::read(uint32_t now, ElectricalReading (&out)[3]) {
    uint32_t status = 0;
    MeasureFault common = fault_;
    if (pending_) {
        if (!bus_.read(address_, 0x0f, status)) common = MeasureFault::Io;
        else if (!(status & 1)) common = now - startedAt_ >= 1000 ? MeasureFault::Timeout : MeasureFault::NotReady;
        else common = MeasureFault::None;
    }
    if (common == MeasureFault::NotReady) return false;
    pending_ = false;
    for (unsigned i = 0; i < 3; ++i) {
        auto& value = out[i]; value = ElectricalReading{}; value.sampledAt = now; value.fault = common;
        if (common != MeasureFault::None) continue;
        uint32_t shunt = 0, bus = 0;
        if (!bus_.read(address_, static_cast<uint8_t>(1 + 2 * i), shunt) ||
            !bus_.read(address_, static_cast<uint8_t>(2 + 2 * i), bus)) {
            value.fault = MeasureFault::Io; continue;
        }
        value.shuntV = signedBits(shunt >> 3, 13) * 40e-6;
        value.voltage = signedBits(bus >> 3, 13) * 0.008;
        const auto& c = channels_[i];
        value.current = value.shuntV / c.ohms * c.polarity;
        value.power = value.voltage * value.current;
        value.valid = value.voltage >= 0 && value.voltage <= c.maxBusV &&
            fabs(value.current) <= c.maxCurrentA && fabs(value.shuntV) < 0.1638;
        value.fault = value.valid ? MeasureFault::None : MeasureFault::Range;
    }
    fault_ = common;
    for (const auto& value : out) if (value.fault == MeasureFault::Io) ready_ = false;
    return true;
}
}
