#pragma once
#include "electrical.h"
#include "i2c_bus.h"
namespace djua {
struct Ina228Config {
    uint8_t address;
    ShuntConfig shunt;
    bool narrowRange = false; // +/-40.96 mV instead of +/-163.84 mV.
};
class Ina228 : public ElectricalSensor {
public:
    Ina228(I2cBus& bus, Ina228Config config) : bus_(bus), config_(config) {}
    bool begin() override;
    bool start(uint32_t now) override;
    ElectricalReading read(uint32_t now) override;
    uint16_t calibration() const { return calibration_; }
    double currentLsb() const { return currentLsb_; }
    MeasureFault fault() const { return fault_; }
private:
    I2cBus& bus_;
    Ina228Config config_;
    uint16_t calibration_ = 0;
    double currentLsb_ = 0;
    bool ready_ = false, pending_ = false;
    uint32_t startedAt_ = 0;
    MeasureFault fault_ = MeasureFault::Disabled;
};
}
