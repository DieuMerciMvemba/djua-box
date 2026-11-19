#include "pzem004t.h"
namespace djua {
uint16_t modbusCrc(const uint8_t* bytes, size_t size) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned b = 0; b < 8; ++b) crc = static_cast<uint16_t>((crc >> 1) ^ ((crc & 1) ? 0xa001 : 0));
    }
    return crc;
}
bool Pzem004t::start(uint32_t now) {
    if (pending_ || address_ == 0 || address_ > 0xf8 || timeoutMs_ == 0) return false;
    // Drop late/unsolicited bytes before a new request. No automatic reset (0x42).
    for (unsigned n = 0; n < 256 && io_.available(); ++n) io_.read();
    if (io_.available()) { fail(MeasureFault::Protocol, now); return false; }
    uint8_t request[8] = {address_, 4, 0, 0, 0, 10, 0, 0};
    const uint16_t crc = modbusCrc(request, 6);
    request[6] = static_cast<uint8_t>(crc); request[7] = static_cast<uint8_t>(crc >> 8);
    pending_ = true; since_ = now; used_ = 0;
    if (!io_.write(request, sizeof(request))) { fail(MeasureFault::Io, now); return false; }
    return true;
}
void Pzem004t::fail(MeasureFault fault, uint32_t now) {
    pending_ = false; used_ = 0; last_ = AcReading{};
    last_.electric.fault = fault; last_.electric.sampledAt = now; ++generation_;
}
void Pzem004t::finish(uint32_t now) {
    const uint16_t crc = modbusCrc(bytes_, used_ - 2);
    if (bytes_[used_ - 2] != (crc & 255) || bytes_[used_ - 1] != (crc >> 8)) {
        fail(MeasureFault::Crc, now); return;
    }
    if (bytes_[1] != 4 || bytes_[2] != 20) { fail(MeasureFault::Protocol, now); return; }
    uint16_t registers[10];
    for (unsigned i = 0; i < 10; ++i)
        registers[i] = static_cast<uint16_t>((bytes_[3 + 2 * i] << 8) | bytes_[4 + 2 * i]);
    AcReading out;
    out.electric.voltage = registers[0] * 0.1;
    out.electric.current = ((static_cast<uint32_t>(registers[2]) << 16) | registers[1]) * 0.001;
    out.electric.power = ((static_cast<uint32_t>(registers[4]) << 16) | registers[3]) * 0.1;
    out.counterWh = (static_cast<uint32_t>(registers[6]) << 16) | registers[5];
    out.frequencyHz = registers[7] * 0.1; out.powerFactor = registers[8] * 0.01;
    out.apparentVA = out.electric.voltage * out.electric.current;
    out.alarm = registers[9] == 0xffff;
    out.electric.sampledAt = now;
    out.electric.valid = out.electric.voltage >= 80 && out.electric.voltage <= 260 &&
        out.electric.current <= 10 && out.electric.power <= 2300 &&
        out.frequencyHz >= 45 && out.frequencyHz <= 65 && out.powerFactor <= 1 &&
        (registers[9] == 0 || registers[9] == 0xffff) &&
        out.electric.power <= out.apparentVA + fmax(2.0, out.apparentVA * .03);
    out.counterValid = out.electric.valid && out.counterWh <= 9999990UL;
    out.electric.fault = out.electric.valid ? MeasureFault::None : MeasureFault::Range;
    last_ = out; pending_ = false; used_ = 0; ++generation_;
}
void Pzem004t::update(uint32_t now) {
    if (!pending_) return;
    for (unsigned n = 0; n < 64 && io_.available() && pending_; ++n) {
        const int c = io_.read(); if (c < 0) break;
        if (used_ == 0 && c != address_) { fail(MeasureFault::Protocol, now); break; }
        bytes_[used_++] = static_cast<uint8_t>(c);
        if (used_ == 2 && bytes_[1] != 4 && bytes_[1] != 0x84) { fail(MeasureFault::Protocol, now); break; }
        if (bytes_[1] == 0x84 && used_ == 5) { finish(now); break; }
        if (used_ == 3 && bytes_[1] == 4 && bytes_[2] != 20) { fail(MeasureFault::Protocol, now); break; }
        if (used_ == sizeof(bytes_)) { finish(now); break; }
    }
    if (pending_ && now - since_ >= timeoutMs_) fail(MeasureFault::Timeout, now);
}
AcReading Pzem004t::reading(uint32_t now, uint32_t maxAge) const {
    AcReading out = last_;
    if (out.electric.valid && now - out.electric.sampledAt > maxAge) {
        out.electric.valid = out.counterValid = false; out.electric.fault = MeasureFault::Timeout;
    }
    return out;
}
}
