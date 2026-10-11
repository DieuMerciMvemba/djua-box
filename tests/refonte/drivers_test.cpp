#include "../../src/drivers/ina228.h"
#include "../../src/drivers/ina3221.h"
#include "../../src/telemetry/energy_window.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <utility>
using namespace djua;

struct Registers : I2cPort {
    std::map<std::pair<uint8_t, uint8_t>, uint32_t> values;
    int failReg = -1;
    bool transfer(uint8_t address, const uint8_t* tx, size_t n, uint8_t* rx, size_t size) override {
        if (tx[0] == failReg) return false;
        auto key = std::make_pair(address, tx[0]);
        if (n == 3) values[key] = (static_cast<uint32_t>(tx[1]) << 8) | tx[2];
        else for (size_t i = 0; i < size; ++i)
            rx[i] = static_cast<uint8_t>(values[key] >> (8 * (size - i - 1)));
        return true;
    }
    uint32_t& reg(uint8_t address, uint8_t reg) { return values[{address, reg}]; }
    void ina228(uint8_t address) {
        reg(address, 0x3e) = 0x5449; reg(address, 0x3f) = 0x2281;
        reg(address, 0x0b) = 2;
        reg(address, 5) = 61440U << 4; // 12 V
        reg(address, 4) = 80000U << 4; // 25 mV -> 10 A
        reg(address, 7) = 174763U << 4;
    }
};
static bool near(double a, double b, double delta = .002) { return fabs(a - b) < delta; }
int main() {
    Registers port; I2cBus bus(port);
    Ina228 battery(bus, {0x40, {.0025, 30, 32, 3, 1}});
    port.ina228(0x40);
    assert(battery.begin() && battery.calibration() == 1875);
    assert(battery.start(0)); auto value = battery.read(200);
    assert(value.valid && near(value.voltage, 12) && near(value.current, 10));
    assert(near(value.power, 120, .02));
    port.reg(0x40, 4) = (1048576U - 80000U) << 4;
    port.reg(0x40, 7) = (1048576U - 174763U) << 4;
    assert(battery.start(1000)); value = battery.read(1200);
    assert(value.valid && value.current < 0 && near(value.power, -120, .02));
    Ina228 duplicate(bus, {0x40, {.0025, 30, 32, 3, 1}});
    assert(!duplicate.begin() && duplicate.fault() == MeasureFault::AddressConflict);
    Ina228 impossible(bus, {0x41, {.0025, 30, 32, 3, 1}, true});
    assert(!impossible.begin()); // 75 mV cannot fit +/-40.96 mV
    Ina228 overheating(bus, {0x41, {.0025, 30, 32, 2, 1}});
    assert(!overheating.begin());
    Ina228 narrow(bus, {0x41, {.0025, 10, 32, 3, -1}, true});
    port.ina228(0x41); assert(narrow.begin() && narrow.calibration() == 2500);
    port.reg(0x41, 4) = 128000U << 4; // 10 mV, narrow range, 4 A
    port.reg(0x41, 7) = 209715U << 4;
    assert(narrow.start(0)); value = narrow.read(200);
    assert(value.valid && near(value.current, -4));
    assert(battery.start(2000)); port.reg(0x40, 0x0b) = 0;
    assert(battery.read(2200).fault == MeasureFault::NotReady);
    assert(battery.read(3000).fault == MeasureFault::Timeout);
    assert(battery.start(4000)); port.reg(0x40, 0x0b) = 0x202;
    assert(battery.read(4200).fault == MeasureFault::Overflow);
    assert(battery.start(5000)); port.reg(0x40, 0x0b) = 2;
    port.reg(0x40, 2) = 0;
    assert(!battery.read(5200).valid); // reset/calibration lost

    const ShuntConfig configs[3] = {{.01, 5, 26, 1, 1}, {.01, 5, 26, 1, -1}, {.01, 5, 26, 1, 1}};
    Ina3221 loads(bus, 0x42, configs);
    port.reg(0x42, 0xfe) = 0x5449; port.reg(0x42, 0xff) = 0x3220;
    port.reg(0x42, 0xf) = 1;
    for (uint8_t channel = 0; channel < 3; ++channel) {
        port.reg(0x42, static_cast<uint8_t>(1 + 2 * channel)) = 250U << 3; // 10 mV -> 1 A
        port.reg(0x42, static_cast<uint8_t>(2 + 2 * channel)) = 1500U << 3;
    }
    assert(loads.begin() && loads.start(0)); ElectricalReading readings[3];
    assert(loads.read(20, readings));
    assert(readings[0].valid && near(readings[0].power, 12));
    assert(readings[1].valid && near(readings[1].power, -12));
    assert(readings[2].valid);
    port.reg(0x42, 5) = 2000U << 3; // 8 A, third port only exceeds configured limit
    assert(loads.start(100)); assert(loads.read(120, readings));
    assert(readings[0].valid && readings[1].valid && !readings[2].valid);
    port.failReg = 3;
    assert(loads.start(200)); assert(loads.read(220, readings));
    assert(readings[0].valid && readings[1].fault == MeasureFault::Io);
    port.failReg = -1;
    EnergyWindow energy;
    energy.sample(UINT32_MAX - 4999, -360, true); energy.sample(5000, -360, true);
    assert(energy.complete(5000) && near(energy.wattHours(), -1));
    energy.sample(15000, 0, false); assert(!energy.complete(15000));
    puts("PASS INA228/INA3221: calibration, polarity, three channels, conflicts, ranges, reset, faults, energy");
}
