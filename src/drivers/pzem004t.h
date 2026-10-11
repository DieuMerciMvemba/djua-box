#pragma once
#include "electrical.h"
#include <stddef.h>
namespace djua {
class ByteStream {
public:
    virtual ~ByteStream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual bool write(const uint8_t* bytes, size_t size) = 0;
};
uint16_t modbusCrc(const uint8_t* bytes, size_t size);
struct AcReading {
    ElectricalReading electric;
    double apparentVA = 0, frequencyHz = 0, powerFactor = 0;
    uint32_t counterWh = 0;
    bool counterValid = false, alarm = false;
};
class Pzem004t {
public:
    Pzem004t(ByteStream& io, uint8_t address = 0xf8, uint32_t timeoutMs = 1000)
        : io_(io), address_(address), timeoutMs_(timeoutMs) {}
    bool start(uint32_t now);
    void update(uint32_t now);
    bool busy() const { return pending_; }
    AcReading reading(uint32_t now, uint32_t maxAge = 5000) const;
    uint32_t generation() const { return generation_; }
private:
    void finish(uint32_t now);
    void fail(MeasureFault fault, uint32_t now);
    ByteStream& io_;
    uint8_t address_, bytes_[25] = {};
    size_t used_ = 0;
    bool pending_ = false;
    uint32_t timeoutMs_, since_ = 0, generation_ = 0;
    AcReading last_;
};
// Counter differences are measured energy, never extrapolated from last power.
// A missing sample, reset, or implausible jump latches incomplete until reset().
class CounterWindow {
public:
    void reset(uint32_t now, uint32_t wh, bool valid) {
        started_ = true; start_ = last_ = now; firstWh_ = lastWh_ = wh;
        complete_ = lastValid_ = valid; fault_ = valid ? MeasureFault::None : MeasureFault::Io;
    }
    void sample(uint32_t now, uint32_t wh, bool valid, uint32_t maxGap = 20000) {
        if (!started_) { reset(now, wh, valid); return; }
        const uint32_t dt = now - last_;
        if (!valid || !lastValid_ || !dt || dt > maxGap) {
            complete_ = false; fault_ = MeasureFault::Timeout;
        } else if (wh < lastWh_) { complete_ = false; fault_ = MeasureFault::CounterReset; }
        else if (wh - lastWh_ > ceil(2300.0 * dt / 3600000.0) + 2) {
            complete_ = false; fault_ = MeasureFault::Range;
        }
        last_ = now; lastWh_ = wh; lastValid_ = valid;
    }
    bool complete(uint32_t now, uint32_t maxAge = 20000) const {
        return started_ && complete_ && now - last_ <= maxAge;
    }
    double wattHours() const { return complete_ ? static_cast<double>(lastWh_ - firstWh_) : 0; }
    MeasureFault fault() const { return fault_; }
private:
    bool started_ = false, complete_ = false, lastValid_ = false;
    uint32_t start_ = 0, last_ = 0, firstWh_ = 0, lastWh_ = 0;
    MeasureFault fault_ = MeasureFault::Disabled;
};
}
