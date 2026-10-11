#include "ina228.h"
namespace djua {
bool Ina228::begin() {
    ready_ = pending_ = false;
    fault_ = MeasureFault::Configuration;
    const double range = config_.narrowRange ? 0.04096 : 0.16384;
    if (config_.address < 0x40 || config_.address > 0x4f ||
        !validShunt(config_.shunt, 85, range)) return false;
    // Leave one signed ADC count of headroom at the configured maximum.
    currentLsb_ = config_.shunt.maxCurrentA / 524287.0;
    const double factor = 13107200000.0 * (config_.narrowRange ? 4 : 1);
    const double cal = floor(factor * currentLsb_ * config_.shunt.ohms + 0.5);
    if (cal < 1 || cal > 32767) return false;
    calibration_ = static_cast<uint16_t>(cal);
    // Use the actual programmed calibration, including its quantization.
    currentLsb_ = calibration_ / (factor * config_.shunt.ohms);
    if (!bus_.claim(config_.address, this)) { fault_ = MeasureFault::AddressConflict; return false; }
    uint32_t manufacturer = 0, device = 0, check = 0;
    fault_ = MeasureFault::Io;
    if (!bus_.read(config_.address, 0x3e, manufacturer) ||
        !bus_.read(config_.address, 0x3f, device)) return false;
    if (manufacturer != 0x5449 || (device >> 4) != 0x228) {
        fault_ = MeasureFault::WrongDevice; return false;
    }
    if (!bus_.write(config_.address, 0, config_.narrowRange ? 0x10 : 0) ||
        !bus_.write(config_.address, 2, calibration_) ||
        !bus_.write(config_.address, 1, 0x0b43) || // shutdown, CT=1052 us, AVG=64
        !bus_.read(config_.address, 2, check) || check != calibration_) return false;
    ready_ = true; fault_ = MeasureFault::None; return true;
}
bool Ina228::start(uint32_t now) {
    if (!ready_ && !begin()) return false;
    pending_ = bus_.write(config_.address, 1, 0x3b43); // bus+shunt, triggered
    startedAt_ = now;
    fault_ = pending_ ? MeasureFault::NotReady : MeasureFault::Io;
    if (!pending_) ready_ = false;
    return pending_;
}
ElectricalReading Ina228::read(uint32_t now) {
    ElectricalReading out; out.fault = fault_; out.sampledAt = now;
    if (!pending_) return out;
    uint32_t diag = 0, calibration = 0, range = 0, shunt = 0, bus = 0, current = 0;
    if (!bus_.read(config_.address, 0x0b, diag)) out.fault = MeasureFault::Io;
    else if (diag & 0x200) out.fault = MeasureFault::Overflow;
    else if (!(diag & 2)) {
        out.fault = now - startedAt_ >= 1000 ? MeasureFault::Timeout : MeasureFault::NotReady;
    } else if (!bus_.read(config_.address, 0, range) ||
        !bus_.read(config_.address, 2, calibration) || calibration != calibration_ ||
        (range & 0x10) != (config_.narrowRange ? 0x10U : 0U) ||
        !bus_.read(config_.address, 4, shunt, 3) || !bus_.read(config_.address, 5, bus, 3) ||
        !bus_.read(config_.address, 7, current, 3)) out.fault = MeasureFault::Io;
    else {
        out.shuntV = signedBits(shunt >> 4, 20) * (config_.narrowRange ? 78.125e-9 : 312.5e-9);
        out.voltage = (bus >> 4) * 195.3125e-6; // VBUS wired on load side
        out.current = signedBits(current >> 4, 20) * currentLsb_ * config_.shunt.polarity;
        out.power = out.voltage * out.current; // signed; hardware POWER is unsigned
        const auto& c = config_.shunt;
        out.valid = out.voltage <= c.maxBusV && fabs(out.current) <= c.maxCurrentA &&
            fabs(out.shuntV) < (config_.narrowRange ? 0.04096 : 0.16384) &&
            fabs(out.current - out.shuntV / c.ohms * c.polarity) <=
                fmax(0.02, fabs(out.current) * 0.02);
        out.fault = out.valid ? MeasureFault::None : MeasureFault::Range;
    }
    if (out.fault != MeasureFault::NotReady) pending_ = false;
    if (out.fault == MeasureFault::Io) ready_ = false;
    fault_ = out.fault; return out;
}
}
