#pragma once
#include "electrical.h"
#include "i2c_bus.h"
namespace djua {
class Ina3221 {
public:
    Ina3221(I2cBus& bus, uint8_t address, const ShuntConfig (&channels)[3])
        : bus_(bus), address_(address) {
        for (unsigned i = 0; i < 3; ++i) channels_[i] = channels[i];
    }
    bool begin();
    bool start(uint32_t now);
    bool read(uint32_t now, ElectricalReading (&out)[3]); // false: conversion pending
    MeasureFault fault() const { return fault_; }
private:
    I2cBus& bus_;
    uint8_t address_;
    ShuntConfig channels_[3];
    bool ready_ = false, pending_ = false;
    uint32_t startedAt_ = 0;
    MeasureFault fault_ = MeasureFault::Disabled;
};
}
